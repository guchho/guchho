This C++ implementation of miniz is from https://github.com/richgel999/miniz/.

## Local divergence from upstream

Upstream miniz always writes `external_attr = 0` (or `0x10` for directories)
and never lets a caller record Unix permission bits, because
`mz_zip_writer_add_mem_ex_v2()` and `mz_zip_writer_add_read_buf_callback()`
initialize their local `ext_attributes` to 0 with no way to supply a value.
Guchho needs real `mode` metadata in ZIP entries, so this copy carries one
additive change:

* `mz_zip_archive` has an extra field, `m_entry_ext_attributes`, defaulting to
  0.
* `mz_zip_writer_set_default_attributes(pZip, ext_attributes)` sets it. It is
  applied to every entry added after the call: `mz_zip_writer_add_mem_ex_v2()`
  and `mz_zip_writer_add_read_buf_callback()` seed their local
  `ext_attributes` from it instead of hardcoding 0, and the existing `|= 0x10`
  for directory names still runs on top.
* In `mz_zip_writer_add_read_buf_callback()`, the local `ext_attributes` was
  widened from `mz_uint16` to `mz_uint32` so it can hold `mode << 16`.

With the setter never called the field stays 0 and output is byte-identical to
upstream miniz, so the default behaviour of every other caller is unchanged.

This must be re-checked if miniz is ever re-vendored: reapply the field, the
setter, the two seeding sites, and the `mz_uint32` widening.
