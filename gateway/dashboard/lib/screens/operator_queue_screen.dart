import 'package:dashboard/services/gateway_api.dart';
import 'package:dashboard/services/operator_queue_controller.dart';
import 'package:dashboard/widgets/error_banner.dart';
import 'package:dashboard/widgets/pending_advisory_card.dart';
import 'package:dashboard/widgets/screen_kit.dart';
import 'package:dashboard/widgets/toast.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

class OperatorQueueScreen extends StatelessWidget {
  /// Controller for the operator queue.
  final OperatorQueueController controller;

  /// Constructor.
  const OperatorQueueScreen({super.key, required this.controller});

  /// Approve an item.
  Future<void> _approve(
    BuildContext context,
    OperatorQueueController c,
    int id,
  ) async {
    try {
      await c.approve(id);
      if (context.mounted) Toast.show(context, 'Approved #$id');
    } on GatewayApiException catch (e) {
      if (context.mounted) Toast.show(context, e.message, error: true);
    }
  }

  /// Reject an item.
  Future<void> _reject(
    BuildContext context,
    OperatorQueueController c,
    int id,
  ) async {
    try {
      await c.reject(id);
      if (context.mounted) Toast.show(context, 'Rejected #$id');
    } on GatewayApiException catch (e) {
      if (context.mounted) Toast.show(context, e.message, error: true);
    }
  }

  // Build the UI.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return ChangeNotifierProvider.value(
      value: controller,
      child: Consumer<OperatorQueueController>(
        builder: (context, c, _) {
          if (c.loading && c.pending.isEmpty) {
            return Center(
              child: CircularProgressIndicator(
                color: theme.colorScheme.primary,
              ),
            );
          }

          return ScreenBody(
            intro: 'Advisories queued for confirmation before action',
            children: [
              if (c.error != null)
                ErrorBanner(message: c.error!, onDismiss: c.clearError),
              Panel(
                title: 'Awaiting approval',
                trailing: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // Icon to show the number of pending item.
                    if (c.pendingCount > 0) ...[
                      StatusChip(
                        label: '${c.pendingCount}',
                        color: theme.colorScheme.tertiary,
                      ),
                      const SizedBox(width: 4),
                    ],
                    // Reload items if required.
                    IconButton(
                      icon: Icon(
                        Icons.refresh,
                        size: 18,
                        color: theme.colorScheme.onSurfaceVariant,
                      ),
                      tooltip: 'Reload',
                      onPressed: c.loading ? null : c.load,
                    ),
                  ],
                ),

                child: c.pending.isEmpty
                    ? // Show when there are no items
                      const CenteredMessage(
                        icon: Icons.check_circle_outline,
                        text: 'Nothing waiting on you right now',
                      )
                    // List all of the pending actions.
                    : ListView.builder(
                        shrinkWrap: true,
                        physics: const NeverScrollableScrollPhysics(),
                        itemCount: c.pending.length,
                        itemBuilder: (context, index) {
                          final advisory = c.pending[index];
                          return PendingAdvisoryCard(
                            advisory: advisory,
                            busy: c.isBusy(advisory.id),
                            onApprove: () => _approve(context, c, advisory.id),
                            onReject: () => _reject(context, c, advisory.id),
                          );
                        },
                      ),
              ),
            ],
          );
        },
      ),
    );
  }
}
