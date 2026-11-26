"""Build C++ JSONL bots as WASI reactors for the browser's local match runner."""
from argparse import ArgumentParser
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / 'web' / 'dist'
OUTPUT = DIST / 'bots'
SOURCES = {
    'weak': DIST / 'examples' / 'weak_bot.cpp',
    'medium': DIST / 'examples' / 'medium_bot.cpp',
    'strong': DIST / 'examples' / 'strong_bot.cpp',
    'neno': ROOT / 'bots' / 'cpp' / 'neno.cpp',
    'northstar': ROOT / 'bots' / 'contenders' / 'northstar.cpp',
}

# Suspend at the existing line boundary, retaining each original local and
# static between requests. Every module instance owns an independent bot.
PRELUDE = r"""
#include <coroutine>
#include <exception>
#include <string>

namespace tetra_web {
const char* input;
size_t size;

struct Task {
    struct promise_type {
        Task get_return_object() {
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}
        [[noreturn]] void unhandled_exception() const { std::terminate(); }
    };
    std::coroutine_handle<promise_type> handle;
};

struct ReadLine {
    std::string& line;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const { line.assign(input, size); }
};
}
"""

ENTRY = r"""
extern "C" void tetra_feed(const char* bytes, size_t length) {
    static auto task = [] {
        auto bot = START();
        bot.handle.resume();
        return bot;
    }();
    tetra_web::input = bytes;
    tetra_web::size = length;
    if (!task.handle.done()) task.handle.resume();
}
"""


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError(f'Expected one occurrence of {before!r}')
    return source.replace(before, after, 1)


def reactor_source(source_path, *, northstar=False):
    source = source_path.read_text(encoding='utf-8')
    if northstar:
        source = replace_once(source, 'void serve() {', 'tetra_web::Task serve() {')
        source = replace_once(source, 'while (getline(cin,line)) {',
                              'for (;;) {\n        co_await tetra_web::ReadLine{line};')
        source = replace_once(source, 'int main() { northstar::serve(); return 0; }', '')
        entry = 'northstar::serve'
    else:
        entry = 'tetra_serve'
        main = re.search(r'\bint\s+main\s*\(\s*(?:void\s*)?\)\s*\{', source)
        if main is None:
            raise ValueError('Expected a parameterless int main() function')
        function = source[main.end():]
        loops = list(re.finditer(
            r'\bwhile\s*\(\s*(?:std::)?getline\s*\(\s*(?:std::)?cin\s*,\s*([a-zA-Z_]\w*)\s*\)\s*\)\s*\{',
            function,
        ))
        if len(loops) != 1:
            raise ValueError('Expected one while (std::getline(std::cin, line)) loop in main()')
        loop = loops[0]
        line = loop.group(1)
        function = (function[:loop.start()] + f'for (;;) {{\n        co_await tetra_web::ReadLine{{{line}}};'
                    + function[loop.end():])
        function = re.sub(r'\breturn\s+0\s*;(\s*\}\s*)$', r'co_return;\1', function)
        source = source[:main.start()] + 'tetra_web::Task tetra_serve() {' + function
    return PRELUDE + source + ENTRY.replace('START', entry)


def build(zig, input_path, output_path, *, northstar=False):
    with TemporaryDirectory(prefix='tetra-bot-') as directory:
        directory = Path(directory)
        source = directory / 'bot.cpp'
        module = directory / 'bot.wasm'
        source.write_text(reactor_source(input_path, northstar=northstar), encoding='utf-8')
        subprocess.run([
            zig, 'c++', '-target', 'wasm32-wasi', '-mexec-model=reactor',
            '-std=c++20', '-O3', '-fno-exceptions', '-fno-rtti', '-Wl,--strip-all',
            '-Wl,--export=tetra_feed', '-Wl,--export=malloc', '-Wl,--export=free',
            '-Wl,-z,stack-size=8388608', '-I', str(input_path.resolve().parent),
            str(source), '-o', str(module),
        ], check=True)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_bytes(module.read_bytes())
        print(f'{output_path}: {output_path.stat().st_size} bytes', flush=True)


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--zig', default='zig')
    parser.add_argument('--source', type=Path, help='a C++ bot that reads JSONL with getline in main()')
    parser.add_argument('--output', type=Path, help='the destination .wasm file for --source')
    parser.add_argument('bots', nargs='*', metavar='BOT', help='built-in names or all five when omitted')
    args = parser.parse_args()
    if args.source:
        if args.bots:
            parser.error('choose --source or built-in names')
        output_path = args.output or args.source.with_suffix('.wasm')
        if args.source.resolve() == output_path.resolve() or output_path.suffix != '.wasm':
            parser.error('choose a separate .wasm output file')
        try:
            build(args.zig, args.source, output_path)
        except ValueError as exc:
            parser.error(str(exc))
        return
    if args.output:
        parser.error('--output requires --source')
    if any(name not in SOURCES for name in args.bots):
        parser.error('built-in choices: ' + ', '.join(SOURCES))

    def build_builtin(name):
        build(args.zig, SOURCES[name], OUTPUT / f'{name}.wasm', northstar=name == 'northstar')

    with ThreadPoolExecutor() as pool:
        list(pool.map(build_builtin, args.bots or SOURCES))


if __name__ == '__main__':
    main()
