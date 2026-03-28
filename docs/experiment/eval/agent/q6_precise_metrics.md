# Q6 Precise Claude Transcript Metrics for `gpu_ext`

Generated: 2026-03-27 03:19:57 UTC

## Scope and Method

- Corpus root: `/home/yunwei37/.claude/projects/-home-yunwei37-workspace-gpu-gpu-ext`
- Primary sessions: 25 top-level `*.jsonl` transcripts
- Nested transcripts: 259 `subagents/*.jsonl` files under per-session directories
- Per-session tables below use the primary transcript only, matching the main session file. Nested subagent work is counted separately in the `Subagent files` and `Subagent lines` columns.
- Aggregate totals are reported in two scopes: `Primary sessions only` and `All transcripts (primary + nested)`.
- Time metrics use every timestamp found in each JSONL record (`timestamp` and `snapshot.timestamp` when present). Active windows split on gaps greater than 30 minutes.
- Token metrics sum `message.usage.input_tokens` and `message.usage.output_tokens` from assistant records only. Cached-token fields are intentionally excluded.
- Bash failure metrics count output lines from Bash `tool_result` records only; assistant reasoning text is excluded.
- Case-study windows are defined as primary-session active windows that contain case-specific markers, then measured across all transcripts under that parent session during those windows.

### Malformed Lines

- `1ffa360b`: 1 malformed line(s), all NUL-only and skipped
- `b1e7bc20`: 1 malformed line(s), all NUL-only and skipped
- `de6eabd4`: 2 malformed line(s), all NUL-only and skipped

## Aggregate Metrics

| Scope | Transcripts | Input tokens | Output tokens | Total tokens | Cost | Summed wall-clock | Summed active span | Tool calls | Builds | Benchmarks | `.bpf.c` edits | Agent spawns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Primary sessions only | 25 | 181,869 | 3,371,848 | 3,553,717 | $255.62 | 713:42 | 87:05 | 7,759 | 199 | 611 | 186 | 133 |
| All transcripts (primary + nested) | 284 | 439,488 | 5,090,736 | 5,530,224 | $388.40 | 1190:43 | 118:39 | 13,640 | 234 | 974 | 244 | 154 |

## All Primary Sessions Summary

| Session | Primary lines | Bad lines | First timestamp | Last timestamp | Wall-clock | Active span | Active windows | Assistant turns | Total tokens | Cost | Tool calls | Builds | Benchmarks | `.bpf.c` edits | Agent spawns | Subagent files | Subagent lines |
| --- | ---: | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 9d654c47 | 12,087 | 0 | 2026-02-23 22:26:25 UTC | 2026-02-25 07:56:41 UTC | 33:30 | 06:10 | 2 | 1,018 | 117,162 | $8.67 | 587 | 15 | 32 | 9 | 0 | 13 | 611 |
| 1499f02c | 3 | 0 | 2026-02-25 05:20:49 UTC | 2026-02-25 06:00:22 UTC | 00:40 | 00:40 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 32c1ebfa | 3 | 0 | 2026-02-25 05:20:49 UTC | 2026-02-25 06:00:22 UTC | 00:40 | 00:40 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| cec13454 | 4 | 0 | 2026-02-25 05:20:49 UTC | 2026-02-25 06:03:37 UTC | 00:43 | 00:43 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 00b4c113 | 9 | 0 | 2026-02-25 07:56:11 UTC | 2026-02-25 08:37:37 UTC | 00:41 | 00:41 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 16156e61 | 5,183 | 0 | 2026-02-25 07:56:11 UTC | 2026-02-25 20:53:03 UTC | 12:57 | 03:50 | 4 | 738 | 42,501 | $3.13 | 433 | 9 | 49 | 7 | 0 | 12 | 528 |
| 84403ce2 | 10 | 0 | 2026-02-25 07:56:11 UTC | 2026-02-25 08:38:09 UTC | 00:42 | 00:42 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| ee60df57 | 8 | 0 | 2026-02-25 07:56:11 UTC | 2026-02-25 08:31:31 UTC | 00:35 | 00:35 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 3b3897bc | 1 | 0 | 2026-02-25 09:13:41 UTC | 2026-02-25 09:13:41 UTC | 00:00 | 00:00 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 9dbdebcd | 8 | 0 | 2026-02-25 18:11:40 UTC | 2026-02-26 07:15:57 UTC | 13:04 | 00:16 | 3 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| d662e303 | 94 | 0 | 2026-02-26 07:59:54 UTC | 2026-02-26 15:24:26 UTC | 07:25 | 00:18 | 2 | 53 | 25,726 | $1.79 | 29 | 0 | 0 | 0 | 0 | 3 | 316 |
| de6eabd4 | 4,665 | 2 | 2026-02-26 20:00:34 UTC | 2026-02-27 04:13:09 UTC | 08:13 | 03:29 | 4 | 735 | 236,157 | $16.32 | 439 | 6 | 49 | 5 | 0 | 17 | 1,315 |
| ab2202db | 5 | 0 | 2026-02-27 01:50:37 UTC | 2026-02-27 02:44:11 UTC | 00:54 | 00:17 | 2 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 6b21980a | 190,190 | 0 | 2026-02-27 04:13:09 UTC | 2026-02-28 04:48:11 UTC | 24:35 | 11:06 | 5 | 2,018 | 530,784 | $39.51 | 1,230 | 53 | 86 | 39 | 4 | 36 | 1,596 |
| ce9a1079 | 179 | 0 | 2026-02-27 23:15:46 UTC | 2026-02-27 23:47:37 UTC | 00:32 | 00:32 | 1 | 50 | 10,393 | $0.77 | 32 | 0 | 1 | 0 | 0 | 1 | 21 |
| 6c0aa1fb | 1,073 | 0 | 2026-02-28 04:51:08 UTC | 2026-02-28 06:27:30 UTC | 01:36 | 01:36 | 1 | 212 | 90,724 | $6.33 | 134 | 2 | 18 | 3 | 2 | 4 | 114 |
| e19dd100 | 22,197 | 0 | 2026-02-28 06:36:01 UTC | 2026-03-02 07:56:27 UTC | 49:20 | 16:26 | 12 | 3,120 | 862,399 | $63.44 | 1,872 | 27 | 191 | 57 | 19 | 36 | 1,410 |
| f9e903b2 | 1 | 0 | 2026-03-02 01:53:57 UTC | 2026-03-02 01:53:57 UTC | 00:00 | 00:00 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| b1e7bc20 | 4,886 | 1 | 2026-03-03 21:31:56 UTC | 2026-03-04 19:22:42 UTC | 21:51 | 05:43 | 6 | 1,445 | 531,913 | $36.54 | 851 | 42 | 13 | 29 | 13 | 22 | 1,135 |
| 648091fb | 6,188 | 0 | 2026-03-04 19:23:32 UTC | 2026-03-05 01:08:53 UTC | 05:45 | 03:08 | 3 | 239 | 80,129 | $4.91 | 154 | 0 | 4 | 0 | 28 | 29 | 5,833 |
| 7cf7718d | 2 | 0 | 2026-03-04 19:23:32 UTC | 2026-03-04 19:23:45 UTC | 00:00 | 00:00 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| e557f3b2 | 1 | 0 | 2026-03-05 01:06:23 UTC | 2026-03-05 01:06:23 UTC | 00:00 | 00:00 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1ffa360b | 21,355 | 1 | 2026-03-05 01:09:39 UTC | 2026-03-27 01:40:19 UTC | 528:31 | 28:46 | 25 | 3,201 | 961,311 | $69.37 | 1,945 | 44 | 165 | 37 | 65 | 84 | 42,075 |
| 7d3f43c8 | 2 | 0 | 2026-03-05 01:09:39 UTC | 2026-03-05 01:12:50 UTC | 00:03 | 00:03 | 1 | 0 | 0 | $0.00 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 05039828 | 256 | 0 | 2026-03-27 01:41:28 UTC | 2026-03-27 03:06:36 UTC | 01:25 | 01:25 | 1 | 110 | 64,518 | $4.83 | 53 | 1 | 3 | 0 | 2 | 2 | 28 |

## Requested Session Details

### Timing and Tokens

| Session | Primary lines | First timestamp | Last timestamp | Wall-clock | Active span | Active windows | Assistant turns | Input tokens | Output tokens | Total tokens | Cost | Subagent files | Subagent lines |
| --- | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 6b21980a | 190,190 | 2026-02-27 04:13:09 UTC | 2026-02-28 04:48:11 UTC | 24:35 | 11:06 | 5 | 2,018 | 5,015 | 525,769 | 530,784 | $39.51 | 36 | 1,596 |
| 1ffa360b | 21,355 | 2026-03-05 01:09:39 UTC | 2026-03-27 01:40:19 UTC | 528:31 | 28:46 | 25 | 3,201 | 45,397 | 915,914 | 961,311 | $69.37 | 84 | 42,075 |
| e19dd100 | 22,197 | 2026-02-28 06:36:01 UTC | 2026-03-02 07:56:27 UTC | 49:20 | 16:26 | 12 | 3,120 | 20,673 | 841,726 | 862,399 | $63.44 | 36 | 1,410 |
| b1e7bc20 | 4,886 | 2026-03-03 21:31:56 UTC | 2026-03-04 19:22:42 UTC | 21:51 | 05:43 | 6 | 1,445 | 55,835 | 476,078 | 531,913 | $36.54 | 22 | 1,135 |
| 9d654c47 | 12,087 | 2026-02-23 22:26:25 UTC | 2026-02-25 07:56:41 UTC | 33:30 | 06:10 | 2 | 1,018 | 2,034 | 115,128 | 117,162 | $8.67 | 13 | 611 |
| 6c0aa1fb | 1,073 | 2026-02-28 04:51:08 UTC | 2026-02-28 06:27:30 UTC | 01:36 | 01:36 | 1 | 212 | 7,973 | 82,751 | 90,724 | $6.33 | 4 | 114 |
| de6eabd4 | 4,665 | 2026-02-26 20:00:34 UTC | 2026-02-27 04:13:09 UTC | 08:13 | 03:29 | 4 | 735 | 23,148 | 213,009 | 236,157 | $16.32 | 17 | 1,315 |
| 648091fb | 6,188 | 2026-03-04 19:23:32 UTC | 2026-03-05 01:08:53 UTC | 05:45 | 03:08 | 3 | 239 | 18,324 | 61,805 | 80,129 | $4.91 | 29 | 5,833 |
| d662e303 | 94 | 2026-02-26 07:59:54 UTC | 2026-02-26 15:24:26 UTC | 07:25 | 00:18 | 2 | 53 | 2,281 | 23,445 | 25,726 | $1.79 | 3 | 316 |
| 16156e61 | 5,183 | 2026-02-25 07:56:11 UTC | 2026-02-25 20:53:03 UTC | 12:57 | 03:50 | 4 | 738 | 879 | 41,622 | 42,501 | $3.13 | 12 | 528 |

### Tool Counts

| Session | Read | Edit | Write | Bash | Grep | Glob | Agent | TaskCreate | TaskUpdate | Total tool calls |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 6b21980a | 209 | 201 | 28 | 509 | 71 | 14 | 4 | 43 | 91 | 1,230 |
| 1ffa360b | 369 | 202 | 46 | 922 | 83 | 51 | 65 | 21 | 59 | 1,945 |
| e19dd100 | 304 | 234 | 25 | 994 | 39 | 4 | 19 | 56 | 124 | 1,872 |
| b1e7bc20 | 149 | 141 | 36 | 341 | 100 | 0 | 13 | 16 | 40 | 851 |
| 9d654c47 | 108 | 67 | 19 | 350 | 8 | 8 | 0 | 3 | 5 | 587 |
| 6c0aa1fb | 38 | 22 | 3 | 51 | 0 | 2 | 2 | 5 | 6 | 134 |
| de6eabd4 | 86 | 37 | 12 | 196 | 31 | 5 | 0 | 19 | 36 | 439 |
| 648091fb | 29 | 36 | 1 | 20 | 2 | 3 | 28 | 10 | 20 | 154 |
| d662e303 | 14 | 0 | 1 | 0 | 0 | 7 | 0 | 0 | 0 | 29 |
| 16156e61 | 72 | 18 | 11 | 264 | 4 | 7 | 0 | 9 | 20 | 433 |

### Action and Failure Metrics

| Session | Builds | Module ops | Benchmarks | Total edits | `.bpf.c` edits | Agent spawns | Bash output lines | Error lines | Xid lines | Verifier lines | Fail lines |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 6b21980a | 53 | 181 | 86 | 229 | 39 | 4 | 8,551 | 146 | 1 | 5 | 80 |
| 1ffa360b | 44 | 194 | 165 | 248 | 37 | 65 | 17,361 | 174 | 1 | 10 | 111 |
| e19dd100 | 27 | 224 | 191 | 259 | 57 | 19 | 21,097 | 167 | 0 | 0 | 73 |
| b1e7bc20 | 42 | 117 | 13 | 177 | 29 | 13 | 6,415 | 178 | 1 | 1 | 52 |
| 9d654c47 | 15 | 53 | 32 | 86 | 9 | 0 | 5,174 | 144 | 0 | 0 | 34 |
| 6c0aa1fb | 2 | 20 | 18 | 25 | 3 | 2 | 1,114 | 58 | 0 | 0 | 15 |
| de6eabd4 | 6 | 63 | 49 | 49 | 5 | 0 | 2,388 | 18 | 0 | 1 | 31 |
| 648091fb | 0 | 1 | 4 | 37 | 0 | 28 | 49 | 5 | 0 | 0 | 2 |
| d662e303 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 16156e61 | 9 | 81 | 49 | 29 | 7 | 0 | 3,932 | 23 | 9 | 1 | 12 |

### Active Windows

- `6b21980a`:
  - window 1: 2026-02-27 04:13:09 UTC -> 2026-02-27 09:20:43 UTC (05:08)
  - window 2: 2026-02-27 17:13:18 UTC -> 2026-02-27 17:38:49 UTC (00:26)
  - window 3: 2026-02-27 18:52:10 UTC -> 2026-02-27 21:15:49 UTC (02:24)
  - window 4: 2026-02-27 22:43:12 UTC -> 2026-02-28 00:49:50 UTC (02:07)
  - window 5: 2026-02-28 03:45:58 UTC -> 2026-02-28 04:48:11 UTC (01:02)
- `1ffa360b`:
  - window 1: 2026-03-05 01:09:39 UTC -> 2026-03-05 02:09:58 UTC (01:00)
  - window 2: 2026-03-05 03:11:27 UTC -> 2026-03-05 08:25:01 UTC (05:14)
  - window 3: 2026-03-05 10:45:55 UTC -> 2026-03-05 10:46:02 UTC (00:00)
  - window 4: 2026-03-05 12:31:10 UTC -> 2026-03-05 12:31:10 UTC (00:00)
  - window 5: 2026-03-05 21:05:21 UTC -> 2026-03-05 22:10:22 UTC (01:05)
  - window 6: 2026-03-06 00:51:49 UTC -> 2026-03-06 01:27:23 UTC (00:36)
  - window 7: 2026-03-06 02:21:32 UTC -> 2026-03-06 03:44:17 UTC (01:23)
  - window 8: 2026-03-06 04:33:50 UTC -> 2026-03-06 09:41:13 UTC (05:07)
  - window 9: 2026-03-06 12:46:46 UTC -> 2026-03-06 12:46:46 UTC (00:00)
  - window 10: 2026-03-06 20:23:13 UTC -> 2026-03-06 20:55:25 UTC (00:32)
  - window 11: 2026-03-06 21:48:20 UTC -> 2026-03-07 00:13:58 UTC (02:26)
  - window 12: 2026-03-07 01:16:55 UTC -> 2026-03-07 01:21:11 UTC (00:04)
  - window 13: 2026-03-07 02:10:34 UTC -> 2026-03-07 05:19:27 UTC (03:09)
  - window 14: 2026-03-07 08:32:36 UTC -> 2026-03-07 09:01:27 UTC (00:29)
  - window 15: 2026-03-07 19:46:17 UTC -> 2026-03-07 19:56:34 UTC (00:10)
  - window 16: 2026-03-07 21:46:55 UTC -> 2026-03-07 23:40:52 UTC (01:54)
  - window 17: 2026-03-19 18:13:50 UTC -> 2026-03-19 20:34:50 UTC (02:21)
  - window 18: 2026-03-19 21:10:30 UTC -> 2026-03-19 21:11:02 UTC (00:01)
  - window 19: 2026-03-19 22:32:49 UTC -> 2026-03-19 22:48:05 UTC (00:15)
  - window 20: 2026-03-20 00:05:17 UTC -> 2026-03-20 00:58:16 UTC (00:53)
  - window 21: 2026-03-24 23:52:51 UTC -> 2026-03-25 00:29:19 UTC (00:36)
  - window 22: 2026-03-25 01:38:13 UTC -> 2026-03-25 01:41:42 UTC (00:03)
  - window 23: 2026-03-25 02:47:38 UTC -> 2026-03-25 03:15:31 UTC (00:28)
  - window 24: 2026-03-26 19:01:28 UTC -> 2026-03-26 19:59:42 UTC (00:58)
  - window 25: 2026-03-27 01:39:17 UTC -> 2026-03-27 01:40:19 UTC (00:01)
- `e19dd100`:
  - window 1: 2026-02-28 06:36:01 UTC -> 2026-02-28 10:40:49 UTC (04:05)
  - window 2: 2026-02-28 14:06:39 UTC -> 2026-02-28 15:18:19 UTC (01:12)
  - window 3: 2026-02-28 19:00:21 UTC -> 2026-02-28 20:24:58 UTC (01:25)
  - window 4: 2026-03-01 03:39:15 UTC -> 2026-03-01 06:08:51 UTC (02:30)
  - window 5: 2026-03-01 06:50:20 UTC -> 2026-03-01 09:18:13 UTC (02:28)
  - window 6: 2026-03-01 18:10:10 UTC -> 2026-03-01 18:49:27 UTC (00:39)
  - window 7: 2026-03-01 21:53:31 UTC -> 2026-03-01 21:53:31 UTC (00:00)
  - window 8: 2026-03-01 23:12:27 UTC -> 2026-03-02 00:11:28 UTC (00:59)
  - window 9: 2026-03-02 01:53:57 UTC -> 2026-03-02 02:01:38 UTC (00:08)
  - window 10: 2026-03-02 02:33:05 UTC -> 2026-03-02 05:07:35 UTC (02:34)
  - window 11: 2026-03-02 05:39:12 UTC -> 2026-03-02 06:05:42 UTC (00:26)
  - window 12: 2026-03-02 07:56:27 UTC -> 2026-03-02 07:56:27 UTC (00:00)
- `b1e7bc20`:
  - window 1: 2026-03-03 21:31:56 UTC -> 2026-03-03 23:00:47 UTC (01:29)
  - window 2: 2026-03-03 23:41:12 UTC -> 2026-03-03 23:51:54 UTC (00:11)
  - window 3: 2026-03-04 04:02:44 UTC -> 2026-03-04 04:26:11 UTC (00:23)
  - window 4: 2026-03-04 04:58:03 UTC -> 2026-03-04 05:34:22 UTC (00:36)
  - window 5: 2026-03-04 06:09:04 UTC -> 2026-03-04 07:29:17 UTC (01:20)
  - window 6: 2026-03-04 17:38:47 UTC -> 2026-03-04 19:22:42 UTC (01:44)
- `9d654c47`:
  - window 1: 2026-02-23 22:26:25 UTC -> 2026-02-24 02:00:22 UTC (03:34)
  - window 2: 2026-02-25 05:20:49 UTC -> 2026-02-25 07:56:41 UTC (02:36)
- `6c0aa1fb`:
  - window 1: 2026-02-28 04:51:08 UTC -> 2026-02-28 06:27:30 UTC (01:36)
- `de6eabd4`:
  - window 1: 2026-02-26 20:00:34 UTC -> 2026-02-26 20:47:16 UTC (00:47)
  - window 2: 2026-02-26 23:09:23 UTC -> 2026-02-26 23:58:34 UTC (00:49)
  - window 3: 2026-02-27 01:47:05 UTC -> 2026-02-27 02:06:34 UTC (00:19)
  - window 4: 2026-02-27 02:39:56 UTC -> 2026-02-27 04:13:09 UTC (01:33)
- `648091fb`:
  - window 1: 2026-03-04 19:23:32 UTC -> 2026-03-04 19:50:24 UTC (00:27)
  - window 2: 2026-03-04 21:07:46 UTC -> 2026-03-04 21:08:21 UTC (00:01)
  - window 3: 2026-03-04 22:28:05 UTC -> 2026-03-05 01:08:53 UTC (02:41)
- `d662e303`:
  - window 1: 2026-02-26 07:59:54 UTC -> 2026-02-26 08:05:04 UTC (00:05)
  - window 2: 2026-02-26 15:11:23 UTC -> 2026-02-26 15:24:26 UTC (00:13)
- `16156e61`:
  - window 1: 2026-02-25 07:56:11 UTC -> 2026-02-25 08:38:09 UTC (00:42)
  - window 2: 2026-02-25 09:13:41 UTC -> 2026-02-25 11:24:30 UTC (02:11)
  - window 3: 2026-02-25 17:31:03 UTC -> 2026-02-25 18:27:53 UTC (00:57)
  - window 4: 2026-02-25 20:52:56 UTC -> 2026-02-25 20:53:03 UTC (00:00)

## Case-Study Windows

| Case | Primary session | Selected active windows | Case first timestamp | Case last timestamp | Wall-clock span | Active span | Transcript count in window | Assistant turns | Input tokens | Output tokens | Total tokens | Cost | Total edits | `.bpf.c` edits | Builds | Benchmarks | Agent spawns |
| --- | --- | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A (FAISS phase-adaptive) | 1ffa360b | 2 | 2026-03-05 01:09:39 UTC | 2026-03-05 08:25:01 UTC | 07:15 | 06:14 | 20 | 1,197 | 7,914 | 219,232 | 227,146 | $16.56 | 44 | 9 | 10 | 42 | 17 |
| B (GPU preemption kfunc) | b1e7bc20 | 6 | 2026-03-03 21:31:56 UTC | 2026-03-04 19:22:42 UTC | 21:51 | 05:43 | 23 | 2,011 | 57,233 | 593,616 | 650,849 | $45.38 | 177 | 29 | 42 | 15 | 13 |
| C (Cross-block multi-stride) | 6b21980a | 5 | 2026-02-27 04:13:09 UTC | 2026-02-28 04:48:11 UTC | 24:35 | 11:06 | 37 | 2,768 | 57,914 | 674,902 | 732,816 | $51.49 | 250 | 59 | 54 | 94 | 4 |

### Case Window Details

- Case A (FAISS phase-adaptive), markers: `Read/Edit/Write on extension/prefetch_faiss_phase*.c`, `Bash command make prefetch_faiss_phase`, `Agent prompt matching Test FAISS phase*`
  - window 1: 2026-03-05 01:09:39 UTC -> 2026-03-05 02:09:58 UTC (01:00)
  - window 2: 2026-03-05 03:11:27 UTC -> 2026-03-05 08:25:01 UTC (05:14)
- Case B (GPU preemption kfunc), markers: `preempt_kfunc`, `test_preempt_kfunc`, `bench_preempt_kfunc`, `gpu_preempt_kfunc_plan`, `bpf_nv_gpu_preempt_tsg`
  - window 1: 2026-03-03 21:31:56 UTC -> 2026-03-03 23:00:47 UTC (01:29)
  - window 2: 2026-03-03 23:41:12 UTC -> 2026-03-03 23:51:54 UTC (00:11)
  - window 3: 2026-03-04 04:02:44 UTC -> 2026-03-04 04:26:11 UTC (00:23)
  - window 4: 2026-03-04 04:58:03 UTC -> 2026-03-04 05:34:22 UTC (00:36)
  - window 5: 2026-03-04 06:09:04 UTC -> 2026-03-04 07:29:17 UTC (01:20)
  - window 6: 2026-03-04 17:38:47 UTC -> 2026-03-04 19:22:42 UTC (01:44)
- Case C (Cross-block multi-stride), markers: `cross_block_prefetch_plan`, `prefetch_cross_block_v2`, `adjacent-stride`, `multi-stride`, `prefetch_adjacent_stride`
  - window 1: 2026-02-27 04:13:09 UTC -> 2026-02-27 09:20:43 UTC (05:08)
  - window 2: 2026-02-27 17:13:18 UTC -> 2026-02-27 17:38:49 UTC (00:26)
  - window 3: 2026-02-27 18:52:10 UTC -> 2026-02-27 21:15:49 UTC (02:24)
  - window 4: 2026-02-27 22:43:12 UTC -> 2026-02-28 00:49:50 UTC (02:07)
  - window 5: 2026-02-28 03:45:58 UTC -> 2026-02-28 04:48:11 UTC (01:02)

## Notable Extremes and Derived Statistics

- Longest single no-break active window: `1ffa360b` at 2026-03-05 03:11:27 UTC -> 2026-03-05 08:25:01 UTC (05:14)
- Most edits in one primary session: `e19dd100` with 259 Edit/Write invocations
- Highest raw Bash error-line count: `b1e7bc20` with 178 matching lines
- Highest Bash error-line rate among sessions with at least 100 Bash output lines: `6c0aa1fb` with 58/1,114 = 5.21%
- Peak tool-use density: `6b21980a` with 254 tool calls in one hour, window 2026-02-27 20:16:05 UTC -> 2026-02-27 21:15:37 UTC; breakdown: Bash=73, Edit=68, Read=30, TaskUpdate=23, Grep=21, Write=12, TaskCreate=10, Task=6, Glob=6, AskUserQuestion=2, ExitPlanMode=2, TaskStop=1
- Inclusive token cost estimate at Claude Opus pricing ($15/M input, $75/M output): $388.40
- Average tokens per benchmark-triggered policy iteration (proxy = total inclusive tokens / total inclusive benchmark commands): 5,677.8
- Thinking/action output-token proxy ratio (inclusive, record-level proxy): 13,160/4,168,626 = 0.0032 (0.32%)
- Subagent spawn vs direct tool work in primary sessions: 133 Agent calls vs 7,626 non-Agent tool calls (1.71% of primary tool invocations were subagent spawns)

## Notes

- `Assistant turns` means raw assistant records in the transcript, not deduplicated human-visible conversation turns. A single interactive turn can emit multiple assistant records (for example thinking, then tool use, then final text).
- The thinking/action split is a proxy based on assistant record type: records that contain `tool_use` blocks are counted as `action`, records with `thinking` but no `tool_use` are counted as `thinking`.
- Case-study metrics are inclusive across nested subagent transcripts during the selected windows. The per-session tables above remain primary-transcript-only to preserve one row per top-level session.
