import 'package:sembast/sembast.dart';

import '../domain/models.dart';
import 'history_store.dart';

class SembastHistoryStore implements HistoryStore {
  SembastHistoryStore({required this.openDatabase});
  final Future<Database> Function() openDatabase;
  Future<Database>? _database;
  static final _plants = stringMapStoreFactory.store('plants');
  static final _readings = stringMapStoreFactory.store('readings');
  static final _events = stringMapStoreFactory.store('irrigations');

  Future<Database> _open() async {
    try {
      return await (_database ??= openDatabase());
    } catch (_) {
      _database = null;
      rethrow;
    }
  }

  @override
  Future<PlantHistory?> read(String plantId) async {
    final db = await _open();
    return db.transaction((txn) async {
      final marker = await _plants.record(plantId).get(txn);
      final finder = Finder(filter: Filter.equals('plantId', plantId));
      final savedReadings = await _readings.find(txn, finder: finder);
      final savedEvents = await _events.find(txn, finder: finder);
      if (marker == null && savedReadings.isEmpty && savedEvents.isEmpty) {
        return null;
      }
      var invalid = false;
      final readings = <SensorReading>[];
      final events = <IrrigationEvent>[];
      for (final record in savedReadings) {
        try {
          readings.add(SensorReading.fromJson(record.value));
        } catch (_) {
          invalid = true;
        }
      }
      for (final record in savedEvents) {
        try {
          events.add(IrrigationEvent.fromJson(record.value));
        } catch (_) {
          invalid = true;
        }
      }
      return PlantHistory(
        readings: readings,
        events: events,
        hasInvalidRecords: invalid,
      );
    });
  }

  @override
  Future<void> append(
    String plantId, {
    List<SensorReading> readings = const [],
    List<IrrigationEvent> events = const [],
  }) async {
    if (readings.any((r) => r.plantId != plantId) ||
        events.any((e) => e.plantId != plantId)) {
      throw ArgumentError('O histórico deve pertencer à mesma planta.');
    }
    final db = await _open();
    await db.transaction((txn) async {
      await _plants.record(plantId).put(txn, {'version': 1});
      for (final r in readings) {
        await _readings.record(readingKey(r)).put(txn, r.toJson());
      }
      for (final e in events) {
        await _events.record('$plantId/${e.id}').put(txn, e.toJson());
      }
    });
  }

  @override
  Future<void> remove(String plantId) async {
    final db = await _open();
    await db.transaction((txn) async {
      final finder = Finder(filter: Filter.equals('plantId', plantId));
      await _readings.delete(txn, finder: finder);
      await _events.delete(txn, finder: finder);
      await _plants.record(plantId).delete(txn);
    });
  }

  @override
  Future<void> clear(
    String plantId, {
    bool readings = true,
    bool events = true,
  }) async {
    final db = await _open();
    await db.transaction((txn) async {
      final finder = Finder(filter: Filter.equals('plantId', plantId));
      if (readings) await _readings.delete(txn, finder: finder);
      if (events) await _events.delete(txn, finder: finder);
      // An empty, initialized history must not be seeded again on restart.
      await _plants.record(plantId).put(txn, {'version': 1});
    });
  }

  @override
  Future<void> close() async {
    final database = _database;
    if (database != null) await (await database).close();
    _database = null;
  }
}
