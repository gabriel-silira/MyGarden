import 'dart:convert';

import '../domain/models.dart';

String readingKey(SensorReading reading) => jsonEncode(reading.toJson());

class PlantHistory {
  const PlantHistory({
    this.readings = const [],
    this.events = const [],
    this.hasInvalidRecords = false,
  });
  final List<SensorReading> readings;
  final List<IrrigationEvent> events;
  final bool hasInvalidRecords;
}

abstract interface class HistoryStore {
  /// Null means this plant has never had a saved history, including an empty one.
  Future<PlantHistory?> read(String plantId);

  /// Atomic and idempotent: retrying a saved batch must not duplicate records.
  Future<void> append(
    String plantId, {
    List<SensorReading> readings = const [],
    List<IrrigationEvent> events = const [],
  });
  Future<void> remove(String plantId);

  /// Atomically empties records while keeping the initialized marker.
  Future<void> clear(
    String plantId, {
    bool readings = true,
    bool events = true,
  });
  Future<void> close();
}

class MemoryHistoryStore implements HistoryStore {
  final Map<String, PlantHistory> _plants = {};
  @override
  Future<PlantHistory?> read(String plantId) async => _plants[plantId];
  @override
  Future<void> append(
    String plantId, {
    List<SensorReading> readings = const [],
    List<IrrigationEvent> events = const [],
  }) async {
    final previous = _plants[plantId];
    _plants[plantId] = PlantHistory(
      readings: {
        for (final r in [...?previous?.readings, ...readings]) readingKey(r): r,
      }.values.toList(),
      events: {
        for (final e in [...?previous?.events, ...events]) e.id: e,
      }.values.toList(),
    );
  }

  @override
  Future<void> remove(String plantId) async => _plants.remove(plantId);
  @override
  Future<void> clear(
    String plantId, {
    bool readings = true,
    bool events = true,
  }) async {
    final old = _plants[plantId];
    _plants[plantId] = PlantHistory(
      readings: readings ? [] : old?.readings ?? [],
      events: events ? [] : old?.events ?? [],
    );
  }

  @override
  Future<void> close() async {}
}
