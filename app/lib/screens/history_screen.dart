import 'package:flutter/material.dart';

import '../state/garden_controller.dart';
import '../widgets/garden_widgets.dart';
import '../widgets/trend_chart.dart';

class HistoryScreen extends StatefulWidget {
  const HistoryScreen({
    super.key,
    required this.controller,
    required this.onAdd,
  });
  final GardenController controller;
  final VoidCallback onAdd;
  @override
  State<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends State<HistoryScreen> {
  int _hours = 24;

  Future<void> _clearHistory(bool readings) async {
    final controller = widget.controller;
    final action = readings ? 'Apagar leituras' : 'Apagar irrigação';
    final kind = readings ? 'leituras' : 'irrigações';
    final plant = controller.selectedPlant;
    if (plant == null || controller.busy) return;
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: Text('$action?'),
        content: Text(
          'Todas as $kind de ${plant.name} serão apagadas, '
          'de todos os períodos. O cadastro da planta será mantido.\n\n'
          'Essa ação não pode ser desfeita. Novas leituras continuarão sendo salvas.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancelar'),
          ),
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: Theme.of(context).colorScheme.error,
              foregroundColor: Theme.of(context).colorScheme.onError,
            ),
            onPressed: () => Navigator.pop(context, true),
            child: Text(action),
          ),
        ],
      ),
    );
    if (confirmed != true || !mounted) return;
    final cleared = await controller.clearHistory(
      plant.id,
      readings: readings,
      events: !readings,
    );
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(
          cleared
              ? 'Registros de $kind de ${plant.name} apagados.'
              : 'Não foi possível apagar o histórico. Tente novamente.',
        ),
      ),
    );
  }

  Widget _sectionHeader(String title, bool readings) => Row(
    children: [
      Expanded(
        flex: 2,
        child: Text(title, style: Theme.of(context).textTheme.titleLarge),
      ),
      const SizedBox(width: 8),
      Flexible(
        flex: 3,
        child: TextButton.icon(
          onPressed: widget.controller.busy
              ? null
              : () => _clearHistory(readings),
          style: TextButton.styleFrom(
            foregroundColor: Theme.of(context).colorScheme.error,
          ),
          icon: const Icon(Icons.delete_outline, size: 18),
          label: FittedBox(
            fit: BoxFit.scaleDown,
            child: Text(
              readings ? 'Apagar leituras' : 'Apagar irrigação',
              maxLines: 1,
              softWrap: false,
            ),
          ),
        ),
      ),
    ],
  );

  @override
  Widget build(BuildContext context) {
    final controller = widget.controller;
    final cutoff = _hours == 0
        ? null
        : DateTime.now().subtract(Duration(hours: _hours));
    final readings = controller.readings
        .where((r) => cutoff == null || !r.timestamp.isBefore(cutoff))
        .toList();
    final events = controller.events
        .where((e) => cutoff == null || !e.timestamp.isBefore(cutoff))
        .toList();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const SectionTitle(
          'Histórico',
          subtitle: 'Pequenas mudanças contam a história do seu jardim.',
        ),
        if (controller.selectedPlant == null)
          EmptyGarden(onAdd: widget.onAdd)
        else ...[
          PlantSelector(controller: controller),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [1, 24, 168, 720, 0]
                .map(
                  (hours) => ChoiceChip(
                    label: Text(switch (hours) {
                      1 => 'Última hora',
                      24 => '24 horas',
                      168 => '7 dias',
                      720 => '30 dias',
                      _ => 'Tudo',
                    }),
                    selected: _hours == hours,
                    onSelected: (_) => setState(() => _hours = hours),
                  ),
                )
                .toList(),
          ),
          const SizedBox(height: 20),
          const Notice(
            'Dados simulados, salvos neste dispositivo mesmo após fechar o app. '
            'As leituras demonstrativas iniciais não são recriadas ao reabrir.',
          ),
          const SizedBox(height: 20),
          Surface(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Umidade do solo',
                  style: Theme.of(context).textTheme.titleLarge,
                ),
                const SizedBox(height: 24),
                TrendChart(readings: readings),
              ],
            ),
          ),
          if (controller.temperatureEnabled) ...[
            const SizedBox(height: 20),
            Surface(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Temperatura',
                    style: Theme.of(context).textTheme.titleLarge,
                  ),
                  const SizedBox(height: 24),
                  TrendChart(readings: readings, temperature: true),
                ],
              ),
            ),
          ],
          const SizedBox(height: 28),
          _sectionHeader('Eventos de irrigação', false),
          EventList(events: events),
          const SizedBox(height: 24),
          _sectionHeader('Leituras do período (${readings.length})', true),
          const SizedBox(height: 12),
          if (readings.isEmpty)
            const Text(
              'Nenhuma leitura neste período. Atualize em Monitoramento.',
            ),
          ...readings.reversed
              .take(50)
              .map(
                (r) => Column(
                  children: [
                    ListTile(
                      contentPadding: EdgeInsets.zero,
                      leading: const Icon(Icons.sensors),
                      title: Text(
                        '${r.moisture.round()}% de umidade${controller.temperatureEnabled && r.temperature != null ? ' • ${r.temperature!.toStringAsFixed(1)} °C' : ''}',
                      ),
                      subtitle: Text('${dateLabel(r.timestamp)} • simulado'),
                    ),
                    const Divider(height: 1),
                  ],
                ),
              ),
          if (readings.length > 50)
            const Padding(
              padding: EdgeInsets.only(top: 16),
              child: Text(
                'Exibindo as 50 leituras mais recentes. O gráfico inclui todas as leituras do período.',
              ),
            ),
        ],
      ],
    );
  }
}
