---
title: Exit codes
description: What a Guchho run returns to the shell.
---

# Exit codes

| Code | Name | Meaning |
| --- | --- | --- |
| `0` | success | The command did what it was asked. Also returned when a run asked nothing - an empty command line, or a lone `--help` / `--version`. |
| `1` | build failure | A build did not succeed. |
| `2` | CLI usage error | The command line was malformed, or asked for something the binary cannot do (for example a service protocol version it does not speak). |
| `130` | interrupted | The run was interrupted (SIGINT). |

The codes are declared in one place as `cli::ExitCode`, so a command that fails
early returns the enum rather than a bare literal.

## See also

- [CLI reference](cli/README.md)
- [Errors & warnings](errors-and-warnings.md)
