#!/usr/bin/env node
// Generates the embedding reference under docs/embedding/generated/ from the
// public C++ headers include/guchho/api.hpp and include/guchho/cli.hpp.
//
// Run:  node scripts/gen-docs-embedding.mjs
// Output: docs/embedding/generated/*.md
//
// Every item is the doc comment from the header followed by the declaration
// as the header spells it. Lines naming tools Guchho does not compare itself
// to in public docs are dropped.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const OUT = path.join(ROOT, 'docs', 'embedding', 'generated')
const GENERATED_NOTE =
  '<!-- GENERATED from include/guchho/api.hpp and include/guchho/cli.hpp by scripts/gen-docs-embedding.mjs. Do not edit by hand. -->'

const SCRUB = [
  /esbuild(?:'s)?[ ]?/gi,
  /rollup(?:'s)?[ ]?/gi,
  /\bvite\b(?:\.config)?(?:'s)?[ ]?/gi,
]

function applyScrub(out) {
  for (const re of SCRUB) out = out.replace(re, '')
  return out
}

function scrub(text) {
  return applyScrub(text)
}

function findCppDeclEnd(src, start) {
  const open = { '(': ')', '[': ']', '{': '}' }
  const stack = []
  const n = src.length
  let str = null
  let i = start
  while (i < n) {
    const c = src[i]
    if (str) {
      if (c === '\\') {
        i += 2
        continue
      }
      if (c === str) str = null
      i++
      continue
    }
    if (c === '/' && src[i + 1] === '/') {
      while (i < n && src[i] !== '\n') i++
      continue
    }
    if (c === '/' && src[i + 1] === '*') {
      i += 2
      while (i + 1 < n && !(src[i] === '*' && src[i + 1] === '/')) i++
      i += 2
      continue
    }
    if (c === '"' || c === "'" || c === '`') {
      str = c
      i++
      continue
    }
    if (c in open) {
      stack.push(c)
      i++
      continue
    }
    if (c === ')' || c === ']' || c === '}') {
      const want = Object.keys(open).find((k) => open[k] === c)
      if (stack.length && stack[stack.length - 1] === want) stack.pop()
      if (c === '}' && stack.length === 0) {
        let j = i + 1
        while (j < n && (src[j] === ' ' || src[j] === '\r')) j++
        if (j < n && src[j] === ';') return j + 1
        return i + 1
      }
      i++
      continue
    }
    if (c === ';' && stack.length === 0) return i + 1
    i++
  }
  return n
}

function looksLikeDecl(line) {
  const t = line.trimStart()
  if (!t) return false
  if (t.startsWith('//') || t.startsWith('/*') || t.startsWith('#')) return false
  if (t.startsWith('namespace ') || t.startsWith('}')) return false
  if (t.endsWith(';') && /^\/\/\s*=$/.test(t)) return false
  if (t.includes('Input:') || t.includes('Output:')) return false
  if (/^\/[*/]/.test(t)) return false
  return true
}

function collectItems(file) {
  const src = fs.readFileSync(file, 'utf8')
  const lines = src.split('\n')
  const starts = [0]
  for (let k = 0; k < lines.length; k++) starts.push(starts[k] + lines[k].length + 1)
  const items = []
  let i = 0
  while (i < lines.length) {
    const trimmed = lines[i].trimStart()
    if (!trimmed.startsWith('//')) {
      i++
      continue
    }
    const block = []
    while (i < lines.length && lines[i].trimStart().startsWith('//')) {
      block.push(lines[i])
      i++
    }
    // A comment block belongs to the declaration that follows it, across any
    // blank lines. Standalone group comments are dropped.
    let j = i
    while (j < lines.length && lines[j].trim() === '') j++
    if (j >= lines.length || !looksLikeDecl(lines[j])) continue
    const start = starts[j]
    const end = findCppDeclEnd(src, start)
    items.push({ doc: block.join('\n'), decl: src.slice(start, end).trim(), line: j })
    while (i < lines.length && starts[i] < end) i++
  }
  return items
}

function slug(name) {
  return name.toLowerCase()
}

function declName(decl) {
  const declHdr = decl.match(/^(?:enum class|struct|class)\s+([A-Za-z_]\w*)/)
  if (declHdr) return declHdr[1]
  const declUsing = decl.match(/^using\s+([A-Za-z_]\w*)/)
  if (declUsing) return declUsing[1]
  let i = 0
  const stack = []
  while (i < decl.length) {
    const c = decl[i]
    if (c === '<' && stack[stack.length - 1] !== '(' && stack[stack.length - 1] !== '<') {
      stack.push('<')
      i++
      continue
    }
    if (c === '>' && stack[stack.length - 1] === '<') {
      stack.pop()
      i++
      continue
    }
    if (c === '(' && !stack.includes('<')) break
    i++
  }
  const sig = decl.slice(0, i).trim()
  const op = sig.match(/operator[^ (]*\s*$/)
  if (op) return op[0].trim()
  const id = sig.match(/([A-Za-z_]\w*)\s*$/)
  if (id) return id[1]
  return null
}

function renderDoc(doc) {
  const lines = doc
    .split('\n')
    .map((l) => l.replace(/^\s*\/\/\s?/, '').replace(/\s+$/, '').replace(/\r/g, ''))
    .filter((l) => {
      if (/^[=\-\s]+$/.test(l)) return false
      return true
    })
    .map((l) => l.replace(/^[\-=]+\s*(.*?)\s*[\-=]+$/, '$1'))
  // Collapse runs of blank lines to a single paragraph break.
  const out = []
  let blank = false
  for (const l of lines) {
    if (!l) {
      blank = true
      continue
    }
    if (blank && out.length) out.push('')
    blank = false
    out.push(l)
  }
  let text = out.join('\n')
  text = scrub(text)
  return text.trim()
}

const PAGES = [
  {
    file: 'enums.md',
    title: 'Enumerations',
    description: 'The scoped enumerations of the guchho engine.',
    blurb:
      'The vocabulary a build is described in. Every decision a caller can make about a build is a scoped enumeration, so option structs stay a flat list of assignments.',
    names: [
      'SourceMap',
      'SourcesContent',
      'LegalComments',
      'JSX',
      'Target',
      'Loader',
      'Platform',
      'Format',
      'Exports',
      'Packages',
      'EngineName',
      'StderrColor',
      'LogLevel',
      'Charset',
      'TreeShaking',
      'DropFlags',
      'MangleQuoted',
      'AbsPathsFlags',
      'SideEffects',
      'ResolveKind',
      'MessageKind',
      'TargetParse',
    ],
  },
  {
    file: 'options.md',
    title: 'Option structs',
    description: 'The plain structs that describe a build, a transform, a server and a watcher.',
    blurb:
      'Configuration is input only. These structs have defaults for every member, so a caller names just the fields it cares about.',
    names: [
      'BuildOptions',
      'TransformOptions',
      'ServeOptions',
      'WatchOptions',
      'FormatMessagesOptions',
      'AnalyzeMetafileOptions',
      'OnResolveOptions',
      'OnLoadOptions',
      'ResolveOptions',
      'MangleCacheResult',
      'ErrorWithNote',
      'ParseOptionsExtras',
    ],
  },
  {
    file: 'types.md',
    title: 'Result and shared types',
    description: 'The types diagnostics, files, target strings and results are expressed in.',
    blurb:
      'Failure is data, not an exception: anything that can go wrong while building arrives as Message entries in a result struct.',
    names: [
      'Engine',
      'TargetSpec',
      'Location',
      'Note',
      'Message',
      'OutputFile',
      'EntryPoint',
      'StdinOptions',
      'ExplicitlySet',
      'BuildResult',
      'TransformResult',
      'EffectiveBuildConfigs',
      'ServeOnRequestArgs',
      'ServeResult',
    ],
  },
  {
    file: 'context.md',
    title: 'Sessions and the server',
    description: 'The long-lived objects: a build context, a running server and the watcher.',
    blurb:
      'A BuildContext is a build that is still alive. Work is either synchronous or owned by an object the caller creates and destroys; nothing is started implicitly.',
    names: ['BuildContext', 'Watcher', 'CORSOptions'],
  },
  {
    file: 'plugins.md',
    title: 'The C++ plugin API',
    description: 'The structs and callbacks through which a C++ embedder extends a build.',
    blurb:
      'A plugin is a struct with a name and a setup function. Setup runs once, when the context is created; the hooks it registers then run for every build of that context.',
    names: [
      'Plugin',
      'PluginBuild',
      'ResolveResult',
      'OnStartResult',
      'OnEndResult',
      'OnResolveArgs',
      'OnResolveResult',
      'OnLoadArgs',
      'OnLoadResult',
    ],
  },
  {
    file: 'api-functions.md',
    title: 'Engine functions',
    description: 'The free functions and constants of include/guchho/api.hpp.',
    blurb:
      'One-shot calls, resolution helpers, the diagnostic renderers and the constants the engine publishes.',
    names: [
      'ParseTargetSpec',
      'ValidEngineNames',
      'Build',
      'ResolveEffectiveBuildConfigs',
      'Transform',
      'Context',
      'FormatMessages',
      'AnalyzeMetafile',
      'strip_dir_prefix',
      'validate_path_template',
      'Serve',
      'RebuildFn',
    ],
  },
  {
    file: 'cli.md',
    title: 'The command line surface',
    description: 'The declarations of include/guchho/cli.hpp.',
    blurb:
      'The boundary between Guchho as a program and Guchho as a library: running a command line, reading arguments without running anything, and the exit codes.',
    names: [
      'Run',
      'RunWithPlugins',
      'RunService',
      'RunServiceRequest',
      'RunServiceCompileRequest',
      'IsServiceCompileCommand',
      'ParseBuildOptions',
      'ParseTransformOptions',
      'ParseLoader',
      'MakeErrorWithNote',
      'parseMangleCache',
      'printMangleCache',
      'ApplyHtmlBundleDefault',
      'ExitCode',
      'Command',
      'ParseOptionsKind',
      'AnalyzeMode',
    ],
  },
]

function writePage(page, byName) {
  const links = []
  const bodies = []
  for (const name of page.names) {
    const it = byName.get(name)
    if (!it) {
      console.warn(`missing declaration: ${name}`)
      continue
    }
    links.push(`- [\`${name}\`](#${slug(name)})`)
    const desc = renderDoc(it.doc)
    const code = scrub(it.decl.replace(/\r/g, ''))
    bodies.push(
      `## ${name}`,
      '',
      desc ? `${desc}\n` : '',
      '```cpp',
      `${code}`,
      '```',
      ''
    )
  }
  const md = [
    '---',
    `title: ${page.title}`,
    `description: ${page.description}`,
    '---',
    '',
    GENERATED_NOTE,
    '',
    `# ${page.title}`,
    '',
    page.blurb,
    '',
    'In this page:',
    '',
    ...links,
    '',
    ...bodies,
  ].join('\n')
  fs.writeFileSync(path.join(OUT, page.file), md)
  return links.length
}

function writeReadme() {
  const sections = []
  for (const page of PAGES) {
    const links = page.names.map((n) => `- [\`${n}\`](${page.file}#${slug(n)})`)
    sections.push(
      `## ${page.title}`,
      '',
      `${page.blurb} [Full page](${page.file}).`,
      '',
      ...links,
      ''
    )
  }
  const md = [
    '---',
    'title: Generated embedding reference',
    'description: The C++ surface, produced from api.hpp and cli.hpp.',
    '---',
    '',
    GENERATED_NOTE,
    '',
    '# Generated embedding reference',
    '',
    'This section is produced from `include/guchho/api.hpp` and',
    '`include/guchho/cli.hpp` by `scripts/gen-docs-embedding.mjs` — the public',
    'C++ surface, transcribed exactly and automatically kept in step. Edit the',
    'generator or the headers, not these pages.',
    '',
    'The [embedding guide](../README.md) is where the reasoning lives; these are',
    'for looking things up.',
    '',
    ...sections,
    '',
    '## Regenerate',
    '',
    '```sh',
    'node scripts/gen-docs-embedding.mjs',
    '```',
    '',
  ].join('\n')
  fs.writeFileSync(path.join(OUT, 'README.md'), md)
}

function main() {
  const byName = new Map()
  for (const file of ['api.hpp', 'cli.hpp']) {
    for (const it of collectItems(path.join(ROOT, 'include', 'guchho', file))) {
      if (/^(?:struct|class) [A-Za-z_]\w*;\s*$/.test(it.decl)) continue
      const name = declName(it.decl)
      if (name && !byName.has(name)) byName.set(name, it)
    }
  }
  fs.mkdirSync(OUT, { recursive: true })
  let total = 0
  for (const page of PAGES) total += writePage(page, byName)
  writeReadme()
  console.log(`wrote ${total} declarations across ${PAGES.length} pages into docs/embedding/generated/`)
}

main()