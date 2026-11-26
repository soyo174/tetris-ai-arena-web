import 'dart:convert';
import 'dart:io';

Future<void> main() async {
  final lines = stdin.transform(utf8.decoder).transform(const LineSplitter());
  await for (final line in lines) {
    final request = jsonDecode(line) as Map<String, dynamic>;
    stdout.writeln(jsonEncode({
      'type': 'PLAY',
      'request_id': request['request_id'],
      'commands': ['HARD_DROP'],
    }));
    await stdout.flush();
  }
}
