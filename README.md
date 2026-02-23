# prettify-json

A fast JSON formatter/prettifier written in C. Reads JSON from stdin and writes
pretty-printed output to stdout.

## Build

```
make
make install   # installs to ~/.local/bin/
```

No external dependencies.

## Usage

```
prettify-json [options] < input.json
```

Reads JSON from stdin, writes formatted JSON to stdout.

```
curl -s https://example.com/api | prettify-json
cat data.json | prettify-json -t
```

## Options

| Option | Description |
|---|---|
| `-t`, `--tabs` | Use tabs instead of spaces for indentation |
| `-s`, `--size N` | Indent N characters per level (default: 2 spaces, 1 tab) |
| `-c`, `--skip-comments` | Skip `//` and `/* */` comments in input |
| `-h`, `--help` | Print help |

When `-t` is used without `-s`, the indent size defaults to 1 tab per level.
