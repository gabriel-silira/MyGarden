enum DataSource { simulated, device }

enum DeviceConnection { simulated, disconnected, connected, error }

enum PumpState { idle, running }

enum IrrigationMode { manual, automatic }

enum SoilCondition { dry, balanced, wet }

enum SimulationScenario { balanced, dry, wet, disconnected, empty }

enum PlantIcon { mint, lemonBalm, chives }

class IrrigationPreferences {
  IrrigationPreferences({
    this.minimum = 35,
    this.target = 65,
    this.mode = IrrigationMode.manual,
  }) {
    if (!minimum.isFinite ||
        !target.isFinite ||
        minimum < 0 ||
        target > 100 ||
        minimum >= target) {
      throw ArgumentError('Os limites devem cumprir 0 ≤ mínimo < alvo ≤ 100.');
    }
  }
  final double minimum;
  final double target;
  final IrrigationMode mode;
  Map<String, dynamic> toJson() => {
    'minimum': minimum,
    'target': target,
    'mode': mode.name,
  };
  factory IrrigationPreferences.fromJson(Map<String, dynamic> json) =>
      IrrigationPreferences(
        minimum: (json['minimum'] as num).toDouble(),
        target: (json['target'] as num).toDouble(),
        mode: IrrigationMode.values.byName(json['mode'] as String),
      );
}

class Plant {
  const Plant({
    required this.id,
    required this.name,
    required this.species,
    required this.notes,
    this.icon = PlantIcon.mint,
    this.deviceId,
    required this.irrigation,
  });
  final String id;
  final String name;
  final String species;
  final String notes;
  final PlantIcon icon;
  final String? deviceId;
  final IrrigationPreferences irrigation;
  Plant copyWith({
    String? name,
    String? species,
    String? notes,
    PlantIcon? icon,
    IrrigationPreferences? irrigation,
  }) => Plant(
    id: id,
    name: name ?? this.name,
    species: species ?? this.species,
    notes: notes ?? this.notes,
    icon: icon ?? this.icon,
    deviceId: deviceId,
    irrigation: irrigation ?? this.irrigation,
  );
  Map<String, dynamic> toJson() => {
    'id': id,
    'name': name,
    'species': species,
    'notes': notes,
    'icon': icon.name,
    'deviceId': deviceId,
    'irrigation': irrigation.toJson(),
  };
  factory Plant.fromJson(Map<String, dynamic> json) => Plant(
    id: json['id'] as String,
    name: json['name'] as String,
    species: json['species'] as String,
    notes: json['notes'] as String,
    icon:
        PlantIcon.values
            .where((icon) => icon.name == json['icon'])
            .firstOrNull ??
        PlantIcon.mint,
    deviceId: json['deviceId'] as String?,
    irrigation: IrrigationPreferences.fromJson(
      json['irrigation'] as Map<String, dynamic>,
    ),
  );
}

class SensorReading {
  SensorReading({
    required this.plantId,
    required this.deviceId,
    required double moisture,
    this.temperature,
    required this.timestamp,
    required this.source,
  }) : moisture = moisture.clamp(0, 100).toDouble() {
    if (!moisture.isFinite || (temperature != null && !temperature!.isFinite)) {
      throw ArgumentError('Medição inválida.');
    }
  }
  final String plantId;
  final String deviceId;
  final double moisture;
  final double? temperature;
  final DateTime timestamp;
  final DataSource source;
  Map<String, dynamic> toJson() => {
    'plantId': plantId,
    'deviceId': deviceId,
    'moisture': moisture,
    'temperature': temperature,
    'timestamp': timestamp.toUtc().toIso8601String(),
    'source': source.name,
  };
  factory SensorReading.fromJson(Map<String, dynamic> json) => SensorReading(
    plantId: json['plantId'] as String,
    deviceId: json['deviceId'] as String,
    moisture: (json['moisture'] as num).toDouble(),
    temperature: (json['temperature'] as num?)?.toDouble(),
    timestamp: DateTime.parse(json['timestamp'] as String).toLocal(),
    source: DataSource.values.byName(json['source'] as String),
  );
  SoilCondition condition(IrrigationPreferences preferences) =>
      moisture < preferences.minimum
      ? SoilCondition.dry
      : moisture > preferences.target
      ? SoilCondition.wet
      : SoilCondition.balanced;
}

class IrrigationEvent {
  const IrrigationEvent({
    required this.id,
    required this.plantId,
    required this.timestamp,
    required this.mode,
    required this.duration,
    required this.source,
  });
  final String id;
  final String plantId;
  final DateTime timestamp;
  final IrrigationMode mode;
  final Duration duration;
  final DataSource source;
  Map<String, dynamic> toJson() => {
    'id': id,
    'plantId': plantId,
    'timestamp': timestamp.toUtc().toIso8601String(),
    'mode': mode.name,
    'durationMicroseconds': duration.inMicroseconds,
    'source': source.name,
  };
  factory IrrigationEvent.fromJson(Map<String, dynamic> json) {
    final duration = json['durationMicroseconds'] as int;
    if (duration < 0) throw const FormatException('Duração inválida.');
    return IrrigationEvent(
      id: json['id'] as String,
      plantId: json['plantId'] as String,
      timestamp: DateTime.parse(json['timestamp'] as String).toLocal(),
      mode: IrrigationMode.values.byName(json['mode'] as String),
      duration: Duration(microseconds: duration),
      source: DataSource.values.byName(json['source'] as String),
    );
  }
}

class GardenException implements Exception {
  const GardenException(this.code, this.message);
  final String code;
  final String message;
  @override
  String toString() => message;
}
