import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sembast/sembast_io.dart';
import 'package:mygardenapp/data/history_store.dart';
import 'package:mygardenapp/data/preferences_store.dart';
import 'package:mygardenapp/data/sembast_history_store.dart';
import 'package:mygardenapp/data/simulated_garden_repository.dart';
import 'package:mygardenapp/domain/models.dart';
import 'package:mygardenapp/state/garden_controller.dart';

void main() {
  GardenController controller(
    HistoryStore history,
    PreferencesStore preferences, {
    SimulatedGardenRepository? repository,
  }) {
    final result = GardenController(
      repository:
          repository ??
          SimulatedGardenRepository(
            latency: Duration.zero,
            irrigationDuration: Duration.zero,
          ),
      preferencesStore: preferences,
      historyStore: history,
    );
    addTearDown(result.dispose);
    return result;
  }

  test('reabre arquivo real e preserva leituras, eventos, datas e isolamento entre plantas', () async {
    final directory = await Directory.systemTemp.createTemp(
      'mygarden-history-test-',
    );
    addTearDown(() => directory.delete(recursive: true));
    final dbPath = '${directory.path}/history.db';
    SembastHistoryStore store() => SembastHistoryStore(
      openDatabase: () => databaseFactoryIo.openDatabase(dbPath),
    );
    final preferences = MemoryPreferencesStore();
    final firstStore = store();
    final first = controller(firstStore, preferences);
    await first.initialize();
    await first.setTheme(ThemeMode.dark);
    await first.irrigate();
    final mintReadings = first.readings.map((r) => r.toJson()).toList();
    final mintEvents = first.events.map((e) => e.toJson()).toList();
    await first.savePlant(
      name: 'Cebolinha',
      species: '',
      notes: '',
      icon: PlantIcon.chives,
    );
    final chivesId = first.selectedId!;
    await first.irrigate();
    final chivesEvents = first.events.map((e) => e.toJson()).toList();
    await firstStore.close();

    final secondStore = store();
    final second = controller(secondStore, preferences);
    await second.initialize();
    expect(second.themeMode, ThemeMode.dark);
    expect(second.readings.map((r) => r.toJson()), containsAll(mintReadings));
    expect(second.readings.length, mintReadings.length + 1);
    expect(second.events.map((e) => e.toJson()), mintEvents);
    second.selectPlant(chivesId);
    expect(second.events.map((e) => e.toJson()), chivesEvents);
    expect(second.readings.every((r) => r.plantId == chivesId), isTrue);
    await second.irrigate();
    expect(second.events.map((e) => e.id).toSet().length, 2);
    await second.removePlant('mint');
    expect(await secondStore.read('mint'), isNull);
    expect((await secondStore.read(chivesId))!.events, hasLength(2));
    await secondStore.close();

    final thirdStore = store();
    final third = controller(thirdStore, preferences);
    await third.initialize();
    expect(third.plants.single.id, chivesId);
    expect(third.events, hasLength(2));
    await thirdStore.close();
  });

  test(
    'instalação antiga mantém cadastro e semeia histórico apenas uma vez',
    () async {
      final preferences = MemoryPreferencesStore();
      final legacy = controller(MemoryHistoryStore(), preferences);
      await legacy.initialize();
      await legacy.setGardenName('Varanda');
      await legacy.setTheme(ThemeMode.light);
      final history = MemoryHistoryStore();
      final migrated = controller(history, preferences);
      await migrated.initialize();
      expect(migrated.gardenName, 'Varanda');
      expect(migrated.readings, hasLength(25));
      final originalDates = migrated.readings.map((r) => r.timestamp).toList();
      final reopened = controller(history, preferences);
      await reopened.initialize();
      expect(reopened.readings, hasLength(26));
      expect(
        reopened.readings.map((r) => r.timestamp),
        containsAll(originalDates),
      );
      expect(reopened.storageWarning, isNull);
    },
  );

  test('não trunca 500 leituras nem 200 irrigações após reabrir', () async {
    final history = MemoryHistoryStore();
    final preferences = MemoryPreferencesStore();
    var now = DateTime(2026, 1, 1);
    final c = controller(
      history,
      preferences,
      repository: SimulatedGardenRepository(
        latency: Duration.zero,
        irrigationDuration: Duration.zero,
        clock: () => now = now.add(const Duration(seconds: 1)),
      ),
    );
    await c.initialize();
    for (var i = 0; i < 205; i++) {
      await c.irrigate();
    }
    for (var i = 0; i < 300; i++) {
      await c.refresh();
    }
    expect(c.readings.length, greaterThan(500));
    expect(c.events, hasLength(205));
    final restored = controller(history, preferences);
    await restored.initialize();
    expect(restored.events, hasLength(205));
    expect(restored.readings.length, c.readings.length + 1);
  });

  test(
    'falha de gravação mantém pendências e repete sem duplicar eventos',
    () async {
      final history = _FailingHistoryStore();
      final preferences = MemoryPreferencesStore();
      final c = controller(history, preferences);
      await c.initialize();
      history.failWrites = true;
      await c.irrigate();
      final eventId = c.events.single.id;
      final reading = c.readings.last.toJson();
      expect(c.storageWarning, contains('histórico'));
      await c.setGardenName('Janela');
      expect(c.storageWarning, contains('histórico'));
      history.failWrites = false;
      await c.refresh();
      expect(c.storageWarning, isNull);
      final reopened = controller(history, preferences);
      await reopened.initialize();
      expect(reopened.events.single.id, eventId);
      expect(reopened.readings.map(readingKey), contains(jsonEncode(reading)));
    },
  );

  test(
    'leitura indisponível não sobrescreve histórico e recupera pendências',
    () async {
      final history = _FailingHistoryStore();
      final preferences = MemoryPreferencesStore();
      final before = controller(history, preferences);
      await before.initialize();
      await before.irrigate();
      final event = before.events.single;
      history.failReads = true;
      final c = controller(history, preferences);
      await c.initialize();
      expect(c.storageWarning, isNotNull);
      expect(c.events, isEmpty);
      history.failReads = false;
      await c.refresh();
      expect(c.events.single.id, event.id);
      expect(c.readings.length, before.readings.length + 2);
      expect(c.storageWarning, isNull);
    },
  );

  test(
    'irrigação concluída é salva mesmo quando a leitura seguinte falha',
    () async {
      final history = MemoryHistoryStore();
      final preferences = MemoryPreferencesStore();
      final c = controller(
        history,
        preferences,
        repository: _ReadFailsAfterIrrigation(),
      );
      await c.initialize();
      await c.irrigate();
      expect(c.error, isNotNull);
      expect(c.events, hasLength(1));
      final restored = controller(history, preferences);
      await restored.initialize();
      expect(restored.events.single.id, c.events.single.id);
    },
  );

  test('erro nas preferências não apaga histórico de outras plantas', () async {
    final history = MemoryHistoryStore();
    final preferences = MemoryPreferencesStore();
    final c = controller(history, preferences);
    await c.initialize();
    await c.savePlant(name: 'Cidreira', species: '', notes: '');
    final id = c.selectedId!;
    await c.irrigate();
    final goodPreferences = preferences.value;
    preferences.value = '{broken';
    final broken = controller(history, preferences);
    await broken.initialize();
    expect(broken.storageWarning, contains('preferências'));
    preferences.value = goodPreferences;
    final recovered = controller(history, preferences);
    await recovered.initialize();
    recovered.selectPlant(id);
    expect(recovered.events, hasLength(1));
  });

  test(
    'limpa uma planta em arquivo real sem recriar histórico ao reabrir',
    () async {
      final directory = await Directory.systemTemp.createTemp(
        'mygarden-clear-test-',
      );
      addTearDown(() => directory.delete(recursive: true));
      SembastHistoryStore store() => SembastHistoryStore(
        openDatabase: () =>
            databaseFactoryIo.openDatabase('${directory.path}/history.db'),
      );
      final preferences = MemoryPreferencesStore();
      final history = store();
      final c = controller(history, preferences);
      await c.initialize();
      await c.irrigate();
      await c.savePlant(name: 'Cidreira', species: '', notes: 'Manter');
      await c.irrigate();
      final otherId = c.selectedId!;
      final otherReadings = c.readings.map((r) => r.toJson()).toList();
      c.selectPlant('mint');
      expect(await c.clearHistory('mint'), isTrue);
      expect(c.plants, hasLength(2));
      expect(c.readings, isEmpty);
      expect(c.events, isEmpty);
      expect((await history.read('mint'))!.readings, isEmpty);
      expect(
        (await history.read(otherId))!.readings.map((r) => r.toJson()),
        otherReadings,
      );
      await history.close();
      final reopenedStore = store();
      final reopened = controller(reopenedStore, preferences);
      await reopened.initialize();
      // Only the fresh startup reading, not the 24 demonstration records.
      expect(reopened.readings, hasLength(1));
      expect(reopened.events, isEmpty);
      await reopened.irrigate();
      expect(reopened.events, hasLength(1));
      reopened.selectPlant(otherId);
      expect(reopened.events, hasLength(1));
      expect(reopened.selectedPlant!.notes, 'Manter');
      await reopenedStore.close();
    },
  );

  test('falha ao limpar mantém histórico e permite tentar novamente', () async {
    final history = _FailingHistoryStore();
    final c = controller(history, MemoryPreferencesStore());
    await c.initialize();
    await c.irrigate();
    final count = c.readings.length;
    history.failClear = true;
    expect(await c.clearHistory('mint'), isFalse);
    expect(c.readings, hasLength(count));
    expect(c.events, hasLength(1));
    expect((await history.read('mint'))!.events, hasLength(1));
    expect(c.storageWarning, contains('Não foi possível apagar'));
    expect(c.busy, isFalse);
    history.failClear = false;
    expect(await c.clearHistory('mint'), isTrue);
    expect(c.storageWarning, isNull);
    await c.refresh();
    expect(c.readings, hasLength(1));
    expect((await history.read('mint'))!.events, isEmpty);
  });

  test('remoção não apaga histórico se salvar cadastro falhar', () async {
    final history = MemoryHistoryStore();
    final preferences = _FailingPreferences();
    final c = controller(history, preferences);
    await c.initialize();
    await c.setGardenName('Persistido');
    await c.irrigate();
    preferences.fail = true;
    await c.removePlant('mint');
    expect(c.storageWarning, isNotNull);
    preferences.fail = false;
    final restored = controller(history, preferences);
    await restored.initialize();
    expect(restored.plants.single.id, 'mint');
    expect(restored.events, hasLength(1));
  });

  test(
    'datas, origem, temperatura nula e duração sobrevivem à serialização',
    () {
      final reading = SensorReading(
        plantId: 'p',
        deviceId: 'd',
        moisture: 12.5,
        timestamp: DateTime.parse('2026-01-02T23:45:00.123456-03:00'),
        source: DataSource.device,
      );
      final restored = SensorReading.fromJson(
        jsonDecode(jsonEncode(reading.toJson())) as Map<String, dynamic>,
      );
      expect(restored.timestamp.isAtSameMomentAs(reading.timestamp), isTrue);
      expect(restored.temperature, isNull);
      expect(restored.source, DataSource.device);
      final event = IrrigationEvent(
        id: 'e',
        plantId: 'p',
        timestamp: reading.timestamp,
        mode: IrrigationMode.automatic,
        duration: const Duration(microseconds: 2345678),
        source: DataSource.device,
      );
      final restoredEvent = IrrigationEvent.fromJson(event.toJson());
      expect(restoredEvent.toJson(), event.toJson());
    },
  );

  test(
    'base recupera registros válidos sem apagar um registro corrompido',
    () async {
      final directory = await Directory.systemTemp.createTemp(
        'mygarden-corrupt-test-',
      );
      addTearDown(() => directory.delete(recursive: true));
      final db = await databaseFactoryIo.openDatabase(
        '${directory.path}/history.db',
      );
      final history = SembastHistoryStore(openDatabase: () async => db);
      final c = controller(history, MemoryPreferencesStore());
      await c.initialize();
      await stringMapStoreFactory.store('readings').record('corrupted').put(
        db,
        {'plantId': 'mint', 'timestamp': 'not-a-date'},
      );
      final reopened = controller(history, MemoryPreferencesStore());
      await reopened.initialize();
      expect(reopened.storageWarning, contains('Alguns registros'));
      expect(reopened.readings.length, c.readings.length + 1);
      expect(
        await stringMapStoreFactory
            .store('readings')
            .record('corrupted')
            .exists(db),
        isTrue,
      );
      await history.close();
    },
  );
}

class _FailingHistoryStore extends MemoryHistoryStore {
  bool failReads = false;
  bool failWrites = false;
  bool failClear = false;
  @override
  Future<void> clear(
    String plantId, {
    bool readings = true,
    bool events = true,
  }) async {
    if (failClear) throw StateError('Clear failed');
    await super.clear(plantId, readings: readings, events: events);
  }

  @override
  Future<PlantHistory?> read(String plantId) async {
    if (failReads) throw StateError('Read failed');
    return super.read(plantId);
  }

  @override
  Future<void> append(
    String plantId, {
    List<SensorReading> readings = const [],
    List<IrrigationEvent> events = const [],
  }) async {
    if (failWrites) throw StateError('Write failed');
    await super.append(plantId, readings: readings, events: events);
  }
}

class _FailingPreferences extends MemoryPreferencesStore {
  bool fail = false;
  @override
  Future<void> write(String value) async {
    if (fail) throw StateError('Write failed');
    await super.write(value);
  }
}

class _ReadFailsAfterIrrigation extends SimulatedGardenRepository {
  _ReadFailsAfterIrrigation()
    : super(latency: Duration.zero, irrigationDuration: Duration.zero);
  bool completed = false;
  @override
  Future<IrrigationEvent> irrigate(
    Plant plant, {
    required IrrigationMode mode,
  }) async {
    final result = await super.irrigate(plant, mode: mode);
    completed = true;
    return result;
  }

  @override
  Future<SensorReading?> read(
    Plant plant, {
    required bool temperatureEnabled,
  }) async {
    if (completed) {
      throw const GardenException('read_failed', 'Leitura indisponível');
    }
    return super.read(plant, temperatureEnabled: temperatureEnabled);
  }
}
