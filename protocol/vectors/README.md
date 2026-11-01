# Message vectors

`messages.tsv` is UTF-8 TSV with four fields per case:
`name<TAB>channel<TAB>expect<TAB>hex`. Blank lines and lines beginning with
`#` are ignored. Names are unique. Channel is `control`, `video`, or `link`
and selects only the payload limit, without direction checks.

Hex is lowercase with no spaces and contains the complete wire message,
including its 6 byte header, except intentional truncation cases. Oversized
length cases contain only the header: limits are checked before waiting for
payload bytes. Expected status is `ok`, `need_more`, `too_large`,
`unknown_type`, `bad_size`, or `bad_enum`.

Complete messages consume header plus declared payload bytes, including
unknown types and invalid known messages. `need_more` and `too_large` consume
zero bytes. Size errors take precedence over enum errors. Every `ok` case
must re-encode byte for byte. Unknown payload bytes are skipped and are not
retained in the model.
