import 'package:flutter/foundation.dart';

/// One problem found in a validator.
@immutable
class ValidatorIssue {
  /// The error message.
  final String message;

  /// Whether or not the error was fatal.
  final bool fatal;

  /// Constructor
  const ValidatorIssue(this.message, {this.fatal = true});
}
