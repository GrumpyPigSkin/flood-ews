// Shared building blocks for the deep-dive screens.

import 'package:flutter/material.dart';
import 'package:dashboard/theme.dart';

/// Standard padded, scrollable screen body with an optional intro line.
class ScreenBody extends StatelessWidget {
  final String? intro;
  final List<Widget> children;

  const ScreenBody({super.key, this.intro, required this.children});

  @override
  Widget build(BuildContext context) {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (intro != null) ...[
            Text(
              intro!,
              style: const TextStyle(color: Palette.textDim, fontSize: 14),
            ),
            const SizedBox(height: 16),
          ],
          ...children,
        ],
      ),
    );
  }
}

/// A titled panel/card used to group content on a screen.
class Panel extends StatelessWidget {
  final String title;
  final Widget? trailing;
  final Widget child;

  const Panel({
    super.key,
    required this.title,
    this.trailing,
    required this.child,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 16),
      decoration: BoxDecoration(
        color: Palette.surface,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Palette.hairline),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 14, 16, 14),
            child: Row(
              children: [
                Text(
                  title,
                  style: const TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w600,
                    color: Palette.text,
                  ),
                ),
                const Spacer(),
                if (trailing != null) trailing!,
              ],
            ),
          ),
          const Divider(height: 1, color: Palette.hairline),
          Padding(padding: const EdgeInsets.all(16), child: child),
        ],
      ),
    );
  }
}
