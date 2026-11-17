import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';

import '../data/garden_repository.dart';
import '../data/preferences_store.dart';
import '../data/history_store.dart';
import '../domain/models.dart';

class GardenController extends ChangeNotifier {
  GardenController({
    required this.repository,
    required this.preferencesStore,
    HistoryStore? historyStore,
  }) : historyStore = historyStore ?? MemoryHistoryStore();
  final GardenRepository repository;
  final PreferencesStore preferencesStore;
  final HistoryStore historyStore;
  final List<Plant> _plants = [
    Plant(
      id: 'mint',
      name: 'Hortelã',
      species: 'Mentha sp.',
      notes:
          'Uma companhia aromática para o seu jardim. '
          'Cadastro demonstrativo: adapte os cuidados à sua planta.',
      deviceId: 'sim-mint',
      irrigation: IrrigationPreferences(),
    ),
  ];
  final Map<String, List<SensorReading>> _history = {};
  final Map<String, SensorReading?> _latest = {};
  final List<IrrigationEvent> _events = [];
  String? selectedId = 'mint';
  String gardenName = 'Meu jardim';
  ThemeMode themeMode = ThemeMode.system;
  bool temperatureEnabled = true;
  bool busy = false;
  bool initialized = false;
  bool _disposed = false;
  PumpState pump = PumpState.idle;
  String? error;
  String? _preferencesWarning;
  String? _historyWarning;
  final Set<String> _damagedHistory = {};
  String? get storageWarning {
    final warnings = [
      _preferencesWarning,
      _historyWarning,
    ].whereType<String>().toList();
    return warnings.isEmpty ? null : warnings.join('\n');
  }

  Future<void> _saveQueue = Future.value();
  Future<void>? _historySaveQueue;
  final Set<String> _historyLoaded = {};
  final Set<String> _pendingHistoryPlants = {};
  final List<SensorReading> _pendingReadings = [];
  final List<IrrigationEvent> _pendingEvents = [];
  List<Plant> get plants => List.unmodifiable(_plants);
  Plant? get selectedPlant =>
      _plants.where((p) => p.id == selectedId).firstOrNull;
  SensorReading? get latest => _latest[selectedId];
  SensorReading? readingFor(String plantId) => _latest[plantId];
  List<SensorReading> get readings =>
      List.unmodifiable(_history[selectedId] ?? []);
  List<IrrigationEvent> get events =>
      List.unmodifiable(_events.where((e) => e.plantId == selectedId));
  SimulationScenario get scenario => repository is SimulationControl
      ? (repository as SimulationControl).scenario
      : SimulationScenario.balanced;
  bool get canIrrigate =>
      !busy &&
      latest != null &&
      error == null &&
      repository.source == DataSource.simulated &&
      repository.connection == DeviceConnection.simulated;
  void _emit() {
    if (!_disposed) notifyListeners();
  }

  Future<void> initialize() async {
    busy = true;
    _emit();
    try {
      final raw = await preferencesStore.read();
      if (raw != null) {
        final data = jsonDecode(raw) as Map<String, dynamic>;
        final restored = (data['plants'] as List)
            .map((p) => Plant.fromJson(p as Map<String, dynamic>))
            .toList();
        final theme = ThemeMode.values.byName(data['theme'] as String);
        final name = data['gardenName'] as String;
        final temperature = data['temperature'] as bool;
        if (name.trim().isEmpty ||
            restored.any((p) => p.name.trim().isEmpty) ||
            restored.map((p) => p.id).toSet().length != restored.length) {
          throw const FormatException('Preferências inválidas');
        }
        _plants
          ..clear()
          ..addAll(restored);
        themeMode = theme;
        gardenName = name;
        temperatureEnabled = temperature;
        selectedId = _plants.firstOrNull?.id;
      }
    } catch (_) {
      _preferencesWarning = 'Não foi possível recuperar as preferências. Usando os padrões desta sessão.';
    }
    // Apply restored appearance while readings are still loading.
    _emit();
    for (final plant in _plants) {
      try {
        await _restoreHistory(plant, seedDemonstration: true);
      } catch (_) {
        _historyWarning = 'Não foi possível abrir o histórico salvo. Os registros anteriores serão preservados.';
      }
      try {
        _record(
          await repository.read(plant, temperatureEnabled: temperatureEnabled),
          plant.id,
        );
      } catch (e) {
        error = _message(e);
      }
    }
    await _flushHistory();
    initialized = true;
    busy = false;
    _emit();
  }

  String _message(Object error) => error is GardenException
      ? error.message
      : 'Não foi possível concluir a operação. Tente atualizar novamente.';
  Future<bool> _save() {
    final value = jsonEncode({
      'version': 1,
      'theme': themeMode.name,
      'gardenName': gardenName,
      'temperature': temperatureEnabled,
      'plants': _plants.map((p) => p.toJson()).toList(),
    });
    final saved = _saveQueue.then((_) async {
      var success = false;
      try {
        await preferencesStore.write(value);
        _preferencesWarning = null;
        success = true;
      } catch (_) {
        _preferencesWarning = 'As alterações valem nesta sessão, mas não puderam ser salvas no dispositivo.';
      }
      _emit();
      return success;
    });
    _saveQueue = saved.then((_) {});
    return saved;
  }

  Future<void> _restoreHistory(
    Plant plant, {
    bool seedDemonstration = false,
  }) async {
    if (_historyLoaded.contains(plant.id)) return;
    final saved = await historyStore.read(plant.id);
    final initial =
        saved?.readings ??
        (seedDemonstration
            ? await repository.history(plant)
            : <SensorReading>[]);
    final combined = <String, SensorReading>{
      for (final reading in [...initial, ...?_history[plant.id]])
        readingKey(reading): reading,
    }.values.toList()..sort((a, b) => a.timestamp.compareTo(b.timestamp));
    _history[plant.id] = combined;
    final events = <String, IrrigationEvent>{
      for (final event in [...?saved?.events, ..._events])
        '${event.plantId}/${event.id}': event,
    }.values.toList()..sort((a, b) => b.timestamp.compareTo(a.timestamp));
    _events
      ..clear()
      ..addAll(events);
    _historyLoaded.add(plant.id);
    if (saved == null) {
      _pendingReadings.addAll(initial);
      _pendingHistoryPlants.add(plant.id);
    }
    if (saved?.hasInvalidRecords ?? false) _damagedHistory.add(plant.id);
  }

  Future<void> _flushHistory() {
    _historySaveQueue = (_historySaveQueue ?? Future<void>.value()).then((
      _,
    ) async {
      var failed = false;
      for (final plant in List<Plant>.of(_plants)) {
        try {
          // A failed read never turns into an overwrite of the existing database.
          await _restoreHistory(plant);
          final readings = _pendingReadings
              .where((r) => r.plantId == plant.id)
              .toList();
          final events = _pendingEvents
              .where((e) => e.plantId == plant.id)
              .toList();
          if (readings.isEmpty &&
              events.isEmpty &&
              !_pendingHistoryPlants.contains(plant.id)) {
            continue;
          }
          await historyStore.append(
            plant.id,
            readings: readings,
            events: events,
          );
          _pendingReadings.removeWhere(readings.contains);
          _pendingEvents.removeWhere(events.contains);
          _pendingHistoryPlants.remove(plant.id);
        } catch (_) {
          failed = true;
        }
      }
      _historyWarning = failed
          ? 'Não foi possível salvar o histórico. Mantenha o app aberto e tente atualizar novamente.'
          : _damagedHistory.isNotEmpty
          ? 'Alguns registros do histórico não puderam ser lidos. Os demais foram recuperados; os dados originais foram preservados.'
          : null;
      _emit();
      _historySaveQueue = null;
    });
    return _historySaveQueue!;
  }

  void selectPlant(String id) {
    if (busy || !_plants.any((p) => p.id == id)) return;
    selectedId = id;
    error = null;
    _emit();
  }

  void _record(SensorReading? reading, String plantId) {
    _latest[plantId] = reading;
    if (reading != null) {
      final history = _history.putIfAbsent(plantId, () => []);
      final key = readingKey(reading);
      if (history.any((r) => readingKey(r) == key)) return;
      history.add(reading);
      history.sort((a, b) => a.timestamp.compareTo(b.timestamp));
      _pendingReadings.add(reading);
    }
  }

  Future<void> refresh() async {
    final plant = selectedPlant;
    if (busy || plant == null) return;
    busy = true;
    error = null;
    _emit();
    try {
      final reading = await repository.read(
        plant,
        temperatureEnabled: temperatureEnabled,
      );
      _record(reading, plant.id);
      if (reading != null &&
          repository.source == DataSource.simulated &&
          plant.irrigation.mode == IrrigationMode.automatic &&
          reading.moisture < plant.irrigation.minimum) {
        await _irrigate(plant, IrrigationMode.automatic);
      }
    } catch (e) {
      error = _message(e);
    } finally {
      await _flushHistory();
      busy = false;
      pump = PumpState.idle;
      _emit();
    }
  }

  Future<void> _irrigate(Plant plant, IrrigationMode mode) async {
    pump = PumpState.running;
    _emit();
    final event = await repository.irrigate(plant, mode: mode);
    _events.insert(0, event);
    _pendingEvents.add(event);
    // Persist a completed irrigation even if its following sensor read fails.
    await _flushHistory();
    _record(
      await repository.read(plant, temperatureEnabled: temperatureEnabled),
      plant.id,
    );
  }

  Future<bool> irrigate() async {
    if (!canIrrigate ||
        selectedPlant!.irrigation.mode != IrrigationMode.manual) {
      return false;
    }
    busy = true;
    error = null;
    _emit();
    var success = false;
    try {
      await _irrigate(selectedPlant!, IrrigationMode.manual);
      success = true;
    } catch (e) {
      error = _message(e);
    } finally {
      await _flushHistory();
      busy = false;
      pump = PumpState.idle;
      _emit();
    }
    return success;
  }

  Future<void> setScenario(SimulationScenario value) async {
    if (busy || repository is! SimulationControl) return;
    (repository as SimulationControl).setScenario(value);
    _latest.clear();
    error = null;
    await refresh();
    _emit();
  }

  Future<void> setTheme(ThemeMode value) async {
    themeMode = value;
    _emit();
    await _save();
  }

  Future<void> setGardenName(String value) async {
    if (value.trim().isEmpty) return;
    gardenName = value.trim();
    _emit();
    await _save();
  }

  Future<void> setTemperature(bool value) async {
    if (busy) return;
    temperatureEnabled = value;
    _emit();
    await _save();
    await refresh();
  }

  Future<void> updateIrrigation(IrrigationPreferences preferences) async {
    final plant = selectedPlant;
    if (plant == null || busy) return;
    _plants[_plants.indexOf(plant)] = plant.copyWith(irrigation: preferences);
    _emit();
    await _save();
  }

  Future<void> savePlant({
    Plant? original,
    PlantIcon? icon,
    required String name,
    required String species,
    required String notes,
  }) async {
    if (busy || name.trim().isEmpty) return;
    if (original != null) {
      final index = _plants.indexWhere((p) => p.id == original.id);
      if (index < 0) return;
      _plants[index] = original.copyWith(
        icon: icon,
        name: name.trim(),
        species: species.trim(),
        notes: notes.trim(),
      );
    } else {
      final id = 'plant-${DateTime.now().microsecondsSinceEpoch}';
      _plants.add(
        Plant(
          id: id,
          icon: icon ?? PlantIcon.mint,
          name: name.trim(),
          species: species.trim(),
          notes: notes.trim(),
          irrigation: IrrigationPreferences(),
        ),
      );
      selectedId = id;
    }
    _emit();
    await _save();
    await refresh();
  }

  Future<bool> clearHistory(
    String plantId, {
    bool readings = true,
    bool events = true,
  }) async {
    if (busy || !_plants.any((p) => p.id == plantId)) return false;
    busy = true;
    _emit();
    try {
      await _historySaveQueue;
      await historyStore.clear(plantId, readings: readings, events: events);
      if (readings) _history[plantId] = [];
      if (events) _events.removeWhere((e) => e.plantId == plantId);
      if (readings) _pendingReadings.removeWhere((r) => r.plantId == plantId);
      if (events) _pendingEvents.removeWhere((e) => e.plantId == plantId);
      _pendingHistoryPlants.remove(plantId);
      _historyLoaded.add(plantId);
      _damagedHistory.remove(plantId);
      // Re-evaluate storage warnings without restoring the deleted records.
      await _flushHistory();
      return true;
    } catch (_) {
      _historyWarning = 'Não foi possível apagar o histórico. Tente novamente.';
      return false;
    } finally {
      busy = false;
      _emit();
    }
  }

  Future<void> removePlant(String id) async {
    if (busy) return;
    busy = true;
    _plants.removeWhere((p) => p.id == id);
    _history.remove(id);
    _latest.remove(id);
    _events.removeWhere((e) => e.plantId == id);
    _pendingReadings.removeWhere((r) => r.plantId == id);
    _pendingEvents.removeWhere((e) => e.plantId == id);
    _pendingHistoryPlants.remove(id);
    _historyLoaded.remove(id);
    if (selectedId == id) selectedId = _plants.firstOrNull?.id;
    error = null;
    _emit();
    if (await _save()) {
      try {
        await _historySaveQueue;
        await historyStore.remove(id);
      } catch (_) {
        _historyWarning = 'A planta foi removida, mas não foi possível apagar seu histórico do dispositivo.';
      }
    }
    busy = false;
    _emit();
  }

  @override
  void dispose() {
    _disposed = true;
    repository.dispose();
    unawaited(
      (_historySaveQueue ?? Future<void>.value())
          .then((_) => historyStore.close())
          .catchError((Object _) {}),
    );
    super.dispose();
  }
}
