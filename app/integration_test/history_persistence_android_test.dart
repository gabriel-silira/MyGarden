import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:mygardenapp/app.dart';
import 'package:mygardenapp/data/local_history_store.dart';
import 'package:mygardenapp/data/preferences_store.dart';
import 'package:mygardenapp/data/simulated_garden_repository.dart';
import 'package:mygardenapp/state/garden_controller.dart';
import 'package:sembast/sembast.dart';
import 'package:shared_preferences/shared_preferences.dart';

// Run write, then read in a second Android process, always with --no-uninstall.
// Flutter otherwise uninstalls the entire app after each phase, deleting even
// the user's data. All records created by this test use test-only keys.
void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  const phase = String.fromEnvironment(
    'historyValidationPhase',
    defaultValue: 'write',
  );
  const preferencesKey = 'mygarden.validation.history.preferences.v1';
  const proofKey = 'mygarden.validation.history.proof.v1';

  testWidgets('histórico em armazenamento Android real: $phase', (
    tester,
  ) async {
    expect(['write', 'read'], contains(phase));
    final preferences = SharedPreferencesAsync();
    final history = createLocalHistoryStore(
      databaseName: 'mygarden-validation-history-v1.db',
    );
    if (phase == 'write') {
      // This database is exclusively for validation, never the user's history.
      final db = await history.openDatabase();
      for (final name in ['plants', 'readings', 'irrigations']) {
        await stringMapStoreFactory.store(name).delete(db);
      }
      await db.close();
      await preferences.remove(preferencesKey);
      await preferences.remove(proofKey);
    }
    final controller = GardenController(
      repository: SimulatedGardenRepository(
        latency: Duration.zero,
        irrigationDuration: Duration.zero,
      ),
      preferencesStore: LocalPreferencesStore(storageKey: preferencesKey),
      historyStore: history,
    );
    addTearDown(controller.dispose);
    await tester.runAsync(controller.initialize);
    expect(controller.storageWarning, isNull);
    await tester.pumpWidget(MyGardenApp(controller: controller));
    await tester.pumpAndSettle();

    if (phase == 'write') {
      await tester.runAsync(() async {
        await controller.setGardenName('Teste do histórico');
        await controller.irrigate();
        final mint = {
          'readings': controller.readings.map((r) => r.toJson()).toList(),
          'events': controller.events.map((e) => e.toJson()).toList(),
        };
        await controller.savePlant(
          name: 'Teste Cidreira',
          species: '',
          notes: 'Validação isolada',
        );
        await controller.irrigate();
        await preferences.setString(
          proofKey,
          jsonEncode({
            'mint': mint,
            controller.selectedId!: {
              'readings': controller.readings.map((r) => r.toJson()).toList(),
              'events': controller.events.map((e) => e.toJson()).toList(),
            },
          }),
        );
      });
      expect(controller.storageWarning, isNull);
    } else {
      final raw = await tester.runAsync(() => preferences.getString(proofKey));
      expect(
        raw,
        isNotNull,
        reason: 'Execute a fase write em outro processo primeiro.',
      );
      final proof = jsonDecode(raw!) as Map<String, dynamic>;
      expect(controller.plants.length, proof.length);
      expect(controller.gardenName, 'Teste do histórico');
      for (final entry in proof.entries) {
        controller.selectPlant(entry.key);
        final saved = entry.value as Map<String, dynamic>;
        final oldReadings = saved['readings'] as List;
        expect(
          controller.readings.map((r) => jsonEncode(r.toJson())),
          containsAll(oldReadings.map(jsonEncode)),
        );
        expect(controller.readings.length, oldReadings.length + 1);
        expect(
          controller.events.map((e) => e.toJson()).toList(),
          saved['events'],
        );
      }
      await tester.runAsync(() async {
        for (final plant in controller.plants.toList()) {
          await controller.removePlant(plant.id);
          expect(await history.read(plant.id), isNull);
        }
        await preferences.remove(preferencesKey);
        await preferences.remove(proofKey);
      });
    }
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    await tester.runAsync(history.close);
  });
}
