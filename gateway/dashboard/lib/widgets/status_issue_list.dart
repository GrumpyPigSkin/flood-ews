import 'package:dashboard/model/external_source.dart';
import 'package:flutter/material.dart';

/// Wrapper to list issues found when validating an external source.
/// Move into it's own widget to not bloat the code.
class StatusIssueList extends StatelessWidget {
  /// The list of issues.
  final List<FieldMapIssue> issues;

  /// Message to display when there are no issues.
  final String validMessage;

  const StatusIssueList({
    super.key,
    required this.issues,
    this.validMessage = 'Configuration is valid',
  });

  /// Build the main UI.
  /// Loop over the issues and display them, if there are no issues display the
  /// valid message.
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    if (issues.isEmpty) {
      return Row(
        children: [
          Icon(
            Icons.check_circle_outline,
            size: 15,
            color: theme.colorScheme.secondary,
          ),
          const SizedBox(width: 8),
          Text(
            validMessage,
            style: TextStyle(fontSize: 12, color: theme.colorScheme.secondary),
          ),
        ],
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        for (final i in issues)
          Padding(
            padding: const EdgeInsets.only(bottom: 6),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Icon(
                  i.fatal ? Icons.error_outline : Icons.info_outline,
                  size: 15,
                  color: i.fatal
                      ? theme.colorScheme.error
                      : theme.colorScheme.tertiary,
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    i.message,
                    style: TextStyle(
                      fontSize: 12,
                      color: i.fatal
                          ? theme.colorScheme.error
                          : theme.colorScheme.tertiary,
                    ),
                  ),
                ),
              ],
            ),
          ),
      ],
    );
  }
}
