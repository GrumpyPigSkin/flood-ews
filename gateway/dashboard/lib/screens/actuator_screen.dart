// Actuators deep-dive, this screen lists the actuator cards, allows listing and
// manual overriding of actuators. Must be logged in to use this screen.

import 'package:dashboard/widgets/error_banner.dart';
import 'package:dashboard/widgets/toast.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'package:dashboard/services/actuator_controller.dart';
import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/widgets/actuator_card.dart';
import 'package:dashboard/widgets/screen_kit.dart';

/// The actuator screen.
class ActuatorsScreen extends StatefulWidget {
  const ActuatorsScreen({super.key});

  @override
  State<ActuatorsScreen> createState() => _ActuatorsScreenState();
}

/// State for the actuator screen.
class _ActuatorsScreenState extends State<ActuatorsScreen> {
  ActuatorController? _controller;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    // Load all the data when the screen is first created, but we don't want to
    // reload all data on every page rebuild.
    _controller ??= ActuatorController(context.read<GatewayApi>())..load();
  }

  /// Dispose of the screen and state.
  @override
  void dispose() {
    _controller?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    // If the controller is loading, show a loading indicator.
    final theme = Theme.of(context);
    final controller = _controller;
    if (controller == null) {
      return Center(
        child: CircularProgressIndicator(color: theme.colorScheme.primary),
      );
    }

    return ChangeNotifierProvider.value(
      value: controller,
      child: Consumer<ActuatorController>(
        builder: (context, c, _) {
          // Wait while we load.
          if (c.isLoading && c.actuators.isEmpty) {
            return Center(
              child: CircularProgressIndicator(
                color: theme.colorScheme.primary,
              ),
            );
          }

          return ScreenBody(
            intro: 'Manual override and actuator configuration. ',
            children: [
              if (c.error != null)
                ErrorBanner(message: c.error!, onDismiss: c.clearError),
              Panel(
                title: 'Actuators',
                trailing: IconButton(
                  icon: Icon(
                    Icons.refresh,
                    size: 18,
                    color: theme.colorScheme.onSurfaceVariant,
                  ),
                  tooltip: 'Reload',
                  onPressed: c.isLoading ? null : c.load,
                ),
                child: c.actuators.isEmpty
                    ? const CenteredMessage(
                        icon: Icons.settings_input_component_outlined,
                        text: 'No actuators configured',
                      )
                    : _ActuatorGrid(controller: c),
              ),
            ],
          );
        },
      ),
    );
  }
}

/// Display the actuators that were retrieved.
class _ActuatorGrid extends StatelessWidget {
  final ActuatorController controller;

  const _ActuatorGrid({required this.controller});

  @override
  Widget build(BuildContext context) {
    final views = controller.actuators;
    return GridView.builder(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
        maxCrossAxisExtent: 600,
        mainAxisSpacing: 14,
        crossAxisSpacing: 14,
        mainAxisExtent: 200,
      ),
      itemCount: views.length,
      itemBuilder: (context, i) {
        final v = views[i];
        return ActuatorCard(
          view: v,
          busy: controller.isBusy(v.spec.id),
          // Actuate callback, manually drive the actuator and toast on return.
          onActuate: (target) async {
            try {
              final applied = await controller.actuate(v.spec.id, target);
              Toast.show(
                context,
                applied
                    ? '${v.spec.name} → $target'
                    : '${v.spec.name} already $target',
              );
            } on GatewayApiException catch (e) {
              Toast.show(context, e.message, error: true);
            }
          },
          // Handle enabling the actuator, toast on enable.
          onSetEnabled: (enabled) async {
            try {
              await controller.setEnable(v.spec, enabled);
              Toast.show(
                context,
                '${v.spec.name} ${enabled ? "enabled" : "disabled"}',
              );
            } on GatewayApiException catch (e) {
              Toast.show(context, e.message, error: true);
            }
          },
        );
      },
    );
  }
}
