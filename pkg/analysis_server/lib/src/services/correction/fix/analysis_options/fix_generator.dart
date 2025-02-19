// Copyright (c) 2019, the Dart project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'dart:math' as math;

import 'package:analysis_server/plugin/edit/fix/fix_core.dart';
import 'package:analysis_server/src/services/correction/fix.dart';
import 'package:analysis_server/src/utilities/strings.dart';
import 'package:analysis_server/src/utilities/yaml_node_locator.dart';
import 'package:analyzer/dart/analysis/session.dart';
import 'package:analyzer/error/error.dart';
import 'package:analyzer/file_system/file_system.dart';
import 'package:analyzer/src/analysis_options/error/option_codes.dart';
import 'package:analyzer/src/generated/java_core.dart';
import 'package:analyzer/src/generated/source.dart';
import 'package:analyzer/src/lint/options_rule_validator.dart';
import 'package:analyzer_plugin/utilities/change_builder/change_builder_core.dart';
import 'package:analyzer_plugin/utilities/change_builder/change_workspace.dart';
import 'package:analyzer_plugin/utilities/fixes/fixes.dart';
import 'package:yaml/yaml.dart';

/// The generator used to generate fixes in analysis options files.
class AnalysisOptionsFixGenerator {
  /// The resource provider used to access the file system.
  final ResourceProvider resourceProvider;

  final AnalysisError error;

  final int errorOffset;

  final int errorLength;

  final String content;

  final YamlMap options;

  final LineInfo lineInfo;

  final List<Fix> fixes = <Fix>[];

  AnalysisOptionsFixGenerator(
      this.resourceProvider, this.error, this.content, this.options)
      : errorOffset = error.offset,
        errorLength = error.length,
        lineInfo = LineInfo.fromContent(content);

  /// Return the absolute, normalized path to the file in which the error was
  /// reported.
  String get file => error.source.fullName;

  /// Return the list of fixes that apply to the error being fixed.
  Future<List<Fix>> computeFixes() async {
    var locator =
        YamlNodeLocator(start: errorOffset, end: errorOffset + errorLength - 1);
    var coveringNodePath = locator.searchWithin(options);
    if (coveringNodePath.isEmpty) {
      return fixes;
    }

    var errorCode = error.errorCode;
//    if (errorCode == AnalysisOptionsErrorCode.INCLUDED_FILE_PARSE_ERROR) {
//    } else if (errorCode == AnalysisOptionsErrorCode.PARSE_ERROR) {
//    } else
    if (errorCode ==
        AnalysisOptionsHintCode.PREVIEW_DART_2_SETTING_DEPRECATED) {
      await _addFix_removeSetting(coveringNodePath);
    } else if (errorCode ==
        AnalysisOptionsHintCode.STRONG_MODE_SETTING_DEPRECATED) {
      await _addFix_removeSetting(coveringNodePath);
    } else if (errorCode == DEPRECATED_LINT_HINT) {
      await _addFix_removeLint(coveringNodePath);
    } else if (errorCode ==
        AnalysisOptionsHintCode.SUPER_MIXINS_SETTING_DEPRECATED) {
      await _addFix_removeSetting(coveringNodePath);
//    } else if (errorCode == AnalysisOptionsWarningCode.INCLUDED_FILE_WARNING) {
//    } else if (errorCode == AnalysisOptionsWarningCode.INCLUDE_FILE_NOT_FOUND) {
//    } else if (errorCode == AnalysisOptionsWarningCode.INVALID_OPTION) {
//    } else if (errorCode == AnalysisOptionsWarningCode.INVALID_SECTION_FORMAT) {
//    } else if (errorCode == AnalysisOptionsWarningCode.SPEC_MODE_REMOVED) {
//    } else if (errorCode ==
//        AnalysisOptionsWarningCode.UNRECOGNIZED_ERROR_CODE) {
    } else if (errorCode ==
        AnalysisOptionsWarningCode.UNSUPPORTED_OPTION_WITHOUT_VALUES) {
      await _addFix_removeSetting(coveringNodePath);
//    } else if (errorCode ==
//        AnalysisOptionsWarningCode.UNSUPPORTED_OPTION_WITH_LEGAL_VALUE) {
//    } else if (errorCode ==
//        AnalysisOptionsWarningCode.UNSUPPORTED_OPTION_WITH_LEGAL_VALUES) {
//    } else if (errorCode == AnalysisOptionsWarningCode.UNSUPPORTED_VALUE) {
    }
    return fixes;
  }

  Future<void> _addFix_removeLint(List<YamlNode> coveringNodePath) async {
    var builder = await _createScalarDeletionBuilder(coveringNodePath);
    if (builder != null) {
      _addFixFromBuilder(builder, AnalysisOptionsFixKind.REMOVE_LINT,
          args: [coveringNodePath[0].toString()]);
    }
  }

  Future<void> _addFix_removeSetting(List<YamlNode> coveringNodePath) async {
    var builder = await _createScalarDeletionBuilder(coveringNodePath);
    if (builder != null) {
      _addFixFromBuilder(builder, AnalysisOptionsFixKind.REMOVE_SETTING,
          args: [coveringNodePath[0].toString()]);
    }
  }

  /// Add a fix whose edits were built by the [builder] that has the given
  /// [kind]. If [args] are provided, they will be used to fill in the message
  /// for the fix.
  void _addFixFromBuilder(ChangeBuilder builder, FixKind kind, {List? args}) {
    var change = builder.sourceChange;
    if (change.edits.isEmpty) {
      return;
    }
    change.message = formatList(kind.message, args);
    fixes.add(Fix(kind, change));
  }

  Future<ChangeBuilder?> _createScalarDeletionBuilder(
    List<YamlNode> coveringNodePath,
  ) async {
    if (coveringNodePath[0] is! YamlScalar) {
      return null;
    }

    SourceRange? deletionRange;
    var index = 1;
    while (index < coveringNodePath.length) {
      var parent = coveringNodePath[index];
      if (parent is YamlList) {
        if (parent.nodes.length > 1) {
          var nodeToDelete = coveringNodePath[index - 1];
          deletionRange = _lines(
              nodeToDelete.span.start.offset, nodeToDelete.span.end.offset);
          break;
        }
      } else if (parent is YamlMap) {
        var nodes = parent.nodes;
        if (nodes.length > 1) {
          YamlNode? key;
          YamlNode? value;
          var child = coveringNodePath[index - 1];
          if (nodes.containsKey(child)) {
            key = child;
            value = nodes[child];
          } else if (nodes.containsValue(child)) {
            for (var entry in nodes.entries) {
              if (child == entry.value) {
                key = entry.key as YamlNode?;
                value = child;
                break;
              }
            }
          }
          if (key == null || value == null) {
            throw StateError(
                'Child is neither a key nor a value in the parent');
          }
          deletionRange = _lines(key.span.start.offset,
              _firstNonWhitespaceBefore(value.span.end.offset));
          break;
        }
      } else if (parent is YamlDocument) {
        break;
      }
      index++;
    }
    var nodeToDelete = coveringNodePath[index - 1];
    deletionRange ??=
        _lines(nodeToDelete.span.start.offset, nodeToDelete.span.end.offset);
    var builder = ChangeBuilder(
      workspace: _NonDartChangeWorkspace(resourceProvider),
    );

    final deletionRange_final = deletionRange;
    await builder.addGenericFileEdit(file, (builder) {
      builder.addDeletion(deletionRange_final);
    });
    return builder;
  }

  int _firstNonWhitespaceBefore(int offset) {
    while (offset > 0 && isWhitespace(content.codeUnitAt(offset - 1))) {
      offset--;
    }
    return offset;
  }

  SourceRange _lines(int start, int end) {
    var startLocation = lineInfo.getLocation(start);
    var startOffset = lineInfo.getOffsetOfLine(startLocation.lineNumber - 1);
    var endLocation = lineInfo.getLocation(end);
    var endOffset = lineInfo.getOffsetOfLine(
        math.min(endLocation.lineNumber, lineInfo.lineCount - 1));
    return SourceRange(startOffset, endOffset - startOffset);
  }
}

class _NonDartChangeWorkspace implements ChangeWorkspace {
  @override
  ResourceProvider resourceProvider;

  _NonDartChangeWorkspace(this.resourceProvider);

  @override
  bool containsFile(String path) {
    return true;
  }

  @override
  AnalysisSession getSession(String path) {
    throw UnimplementedError('Attempt to work a Dart file.');
  }
}
