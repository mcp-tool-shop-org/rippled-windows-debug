import type { SiteConfig } from '@mcptoolshop/site-theme';

export const config: SiteConfig = {
  title: 'rippled-windows-debug',
  description: 'Windows debugging toolkit for rippled (XRPL validator node).',
  logoBadge: 'RD',
  brandName: 'rippled-windows-debug',
  repoUrl: 'https://github.com/mcp-tool-shop-org/rippled-windows-debug',
  footerText: 'MIT Licensed — built by <a href="https://github.com/mcp-tool-shop-org" style="color:var(--color-muted);text-decoration:underline">MCP Tool Shop</a>',

  hero: {
    badge: 'Open source',
    headline: 'Debug rippled',
    headlineAccent: 'on Windows.',
    description: 'Build governor prevents OOM crashes. Verbose crash handlers reveal the real exception behind STATUS_STACK_BUFFER_OVERRUN. Rich-style logging for C++ builds.',
    primaryCta: { href: '#tools', label: 'See the toolkit' },
    secondaryCta: { href: 'https://github.com/mcp-tool-shop-org/rippled-windows-debug', label: 'GitHub' },
    previews: [
      { label: 'Setup', code: '.\\scripts\\setup-governor.ps1\n# All builds are now protected automatically' },
      { label: 'Build', code: 'cmake --build build --parallel 16\n# Governor prevents OOM automatically' },
      { label: 'Crash', code: 'Type:    std::bad_alloc\nMessage: bad allocation\n# Not STATUS_STACK_BUFFER_OVERRUN!' },
    ],
  },

  sections: [
    {
      kind: 'features',
      id: 'tools',
      title: 'The Toolkit',
      subtitle: 'Five tools for building and debugging rippled on Windows.',
      features: [
        { title: 'Build Governor', desc: 'Monitors commit charge and throttles parallel cl.exe processes. Zero-config — wrappers auto-start on first build.' },
        { title: 'Crash handlers', desc: 'Single-header diagnostics that reveal std::bad_alloc hidden as STATUS_STACK_BUFFER_OVERRUN. Full stack traces with symbols.' },
        { title: 'Rich-style logging', desc: 'Colored log levels, box-drawing section boundaries, automatic timing, and correlation IDs for C++ terminal output.' },
      ],
    },
    {
      kind: 'data-table',
      id: 'components',
      title: 'Components',
      subtitle: 'All single-header C++ files plus the .NET governor.',
      columns: ['File', 'What it does'],
      rows: [
        ['crash_handlers.h', 'Verbose crash diagnostics with exception type, stack trace, and system info'],
        ['debug_log.h', 'Rich-style terminal logging with color, timing, and correlation IDs'],
        ['minidump.h', 'Automatic crash dump capture with configurable location'],
        ['build_info.h', 'Toolkit version, git commit, compiler, OS, CPU, and memory info'],
        ['build-governor/', '.NET service that throttles parallel builds based on commit charge'],
      ],
    },
    {
      kind: 'code-cards',
      id: 'quickstart',
      title: 'Quick start',
      cards: [
        { title: 'Protect builds', code: '# One-time setup (no admin required)\n.\\scripts\\setup-governor.ps1\n\n# Restart terminal, then build safely\ncmake --build build --parallel 16\n# Governor prevents OOM automatically' },
        { title: 'Patch rippled', code: '#if BOOST_OS_WINDOWS\n#include "crash_handlers.h"\n#endif\n\nint main() {\n#if BOOST_OS_WINDOWS\n    installVerboseCrashHandlers();\n#endif\n}' },
      ],
    },
    {
      kind: 'features',
      id: 'problem',
      title: 'The Problem',
      subtitle: 'Why parallel C++ builds on Windows crash silently.',
      features: [
        { title: 'Memory exhaustion', desc: 'Each cl.exe uses 1-4 GB RAM. High -j values exhaust commit charge, causing silent failures.' },
        { title: 'Misleading errors', desc: 'STATUS_STACK_BUFFER_OVERRUN (0xC0000409) is really std::bad_alloc — the /GS check masks it.' },
        { title: 'System freezes', desc: 'When commit charge hits 100%, Windows becomes unresponsive. The governor throttles before that happens.' },
      ],
    },
  ],
};
