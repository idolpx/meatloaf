# Class diagrams

Mermaid class diagrams for the Meatloaf codebase, verified against the
source tree (see the date stamp in each diagram header).

## Layout

- `defs/<Class>.mmd` - ONE canonical definition per class: the complete
  `class X { ... }` block, members ordered private (`-`), protected (`#`),
  public (`+`). Public members whose name is not self-describing carry a
  short ` - description` suffix.
- `src/<diagram>.mmd` - diagram templates: header comments, layout, bare
  class nodes, namespaces, relationships, notes, and `%%include <Class>`
  lines where a full class body belongs.
- `build.js` - assembles `src/*.mmd` + `defs/*.mmd` into the top-level
  `<diagram>.mmd` files (which are committed so they render on GitHub).
- `<diagram>.mmd` - GENERATED. Do not edit by hand; edit `defs/` or
  `src/` and rebuild.

## Building

    cd docs/classes
    node build.js          # regenerate the top-level .mmd files
    node build.js check    # CI-style check: fails if outputs are stale

A class used by several diagrams is included from the same def file in
each, so its definition can never drift between diagrams. Overview
diagrams (architecture.mmd) intentionally use bare nodes.

## Diagrams

| file | contents |
|---|---|
| architecture.mmd | module overview (bare nodes + relations) |
| core_hierarchy.mmd | MStream/MFile/MFileSystem/MFSOwner, brokers, sessions, iostream buffer |
| stream_hierarchy.mmd | every MStream implementation |
| mfile_hierarchy.mmd | every MFile implementation + filesystem registry |
| media_scanner.mmd | media containers, D64 family, TAPClean tape pipeline |
| network_services.mmd | protocol sessions, HTTP client, sockets, mDNS, web server |
| hardware_encoding.mmd | IEC bus + devices, channel handlers, protocols, hardware, console, utils |
| loaders_emulation.mmd | TAPClean engine internals, codecs (retropixels/QR), HIMEM, SAM |
