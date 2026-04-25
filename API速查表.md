# 信号处理新方法 API 逐函数速查表

这份文档专门按“一个公开函数一个条目”的方式来写。每个条目都固定写 4 件事：

- 输入
- 输出
- 典型错误
- 推荐调用顺序

如果你只想先跑通主链，先看下面这条总顺序：

```text
psa_collect_default_config()
-> psa_collect_init()
-> psa_core_init()
-> 循环调用 psa_collect_push_xxx() 或 psa_core_push_value()
-> psa_core_get_summary() / psa_core_get_certificate()
-> psa_explain_capture_record()
-> psa_explain_export_series() / psa_explain_replay_rule()
-> 如需录段：psa_explain_segment_recorder_arm_from_record() -> update() -> completed() -> get_segment()
-> 如需流式播放：psa_explain_stream_player_begin_xxx() -> next()
-> 如需外置直解：psa_direct_decode_xxx() -> blocks/family/object -> reconstruct/synthesize
```

注意：

- `psa_core` 是主链，证书和结构结论以它为主。
- `psa_explain` 是主链下游解释层，用来抓记录、导出、回放、录段、重放。
- `psa_direct` 是外置可结合直解层，不是主链判据。

---

## 1. `psa_base`

### `psa_real_abs(value)`
- 输入：单个 `psa_real_t`。
- 输出：返回绝对值。
- 典型错误：纯函数，无状态错误。
- 推荐调用顺序：任意位置直接调用。

### `psa_real_max(a, b)`
- 输入：两个 `psa_real_t`。
- 输出：返回较大值。
- 典型错误：纯函数，无状态错误。
- 推荐调用顺序：任意位置直接调用。

### `psa_real_machine_epsilon()`
- 输入：无。
- 输出：返回当前 `PSA_REAL_TYPE` 对应的机器精度。
- 典型错误：无；但结果依赖你编译时把 `PSA_REAL_TYPE` 配成什么。
- 推荐调用顺序：做误差阈值、尺度容差、报告打印前调用。

### `psa_real_scaled_epsilon(scale)`
- 输入：对象尺度 `scale`。
- 输出：返回 `max(1, scale) * machine_epsilon`。
- 典型错误：无；`scale < 1` 时内部会自动按 `1` 保底。
- 推荐调用顺序：写尺度相关容差前调用。

### `psa_dd_from_real(value)`
- 输入：一个普通实数。
- 输出：对应的 `psa_dd_real_t` 双双精度值。
- 典型错误：纯函数，无状态错误。
- 推荐调用顺序：需要 DD 工作值时先调。

### `psa_dd_add(a, b)`
- 输入：两个 `psa_dd_real_t`。
- 输出：DD 和。
- 典型错误：纯函数，无状态错误。
- 推荐调用顺序：DD 累加时直接用。

### `psa_dd_sub(a, b)`
- 输入：两个 `psa_dd_real_t`。
- 输出：DD 差。
- 典型错误：纯函数，无状态错误。
- 推荐调用顺序：DD 差分时直接用。

### `psa_dd_scale_pow2(value, exponent)`
- 输入：DD 值、2 的指数。
- 输出：二幂缩放后的 DD 值。
- 典型错误：纯函数，无状态错误。
- 推荐调用顺序：需要精确二幂重标度时调用。

### `psa_dd_to_real(value)`
- 输入：DD 值。
- 输出：回落成普通 `psa_real_t`。
- 典型错误：无；但会失去 DD 的额外精度。
- 推荐调用顺序：最终输出或写回普通数组时调用。

### `psa_scale_pow2_real(value, exponent)`
- 输入：普通实数、指数。
- 输出：二幂缩放后的普通实数。
- 典型错误：纯函数，无状态错误。
- 推荐调用顺序：主链或解释层做 pow2 重标度时调用。

### `psa_scale_pow2_real_array(values, count, exponent)`
- 输入：普通实数数组、长度、指数。
- 输出：原地修改数组。
- 典型错误：`values == NULL` 或 `exponent == 0` 时直接返回，不报错。
- 推荐调用顺序：批量重标度普通实数数组时调用。

### `psa_scale_pow2_dd_array(values, count, exponent)`
- 输入：DD 数组、长度、指数。
- 输出：原地修改数组。
- 典型错误：`values == NULL` 或 `exponent == 0` 时直接返回，不报错。
- 推荐调用顺序：批量重标度 DD 数组时调用。

### `psa_kahan_reset(acc)`
- 输入：`psa_kahan_t*`。
- 输出：把累加器清零。
- 典型错误：`acc == NULL` 时无动作。
- 推荐调用顺序：任何 Kahan 累加前先调。

### `psa_kahan_add(acc, value)`
- 输入：Kahan 累加器、一个值。
- 输出：更新累加器内部和补偿项。
- 典型错误：`acc == NULL` 时无动作。
- 推荐调用顺序：`psa_kahan_reset()` 之后循环调用。

### `psa_fnv1a_mix(hash, chunk)`
- 输入：旧哈希、一个 64 位块。
- 输出：新的 FNV-1a 混合结果。
- 典型错误：纯函数，无状态错误。
- 推荐调用顺序：手工组合签名时调用。

### `psa_quantize_value(value, scale)`
- 输入：实数值、对象尺度。
- 输出：量化后的 `int64_t`。
- 典型错误：不是错误码接口；接近尺度 epsilon 的值会被压成 `0`。
- 推荐调用顺序：一般只在做签名量化前调用。

### `psa_signature_from_reals(values, count, scale)`
- 输入：实数数组、长度、尺度。
- 输出：64 位签名。
- 典型错误：`values == NULL` 时返回初始哈希，不抛错。
- 推荐调用顺序：需要对序列做对象签名时调用。

### `psa_summary_reset(summary)`
- 输入：`psa_summary_t*`。
- 输出：摘要结构清零。
- 典型错误：`summary == NULL` 时无动作。
- 推荐调用顺序：手工构造或复用摘要对象前调用。

### `psa_status_string(status)`
- 输入：`psa_status_t`。
- 输出：状态字符串，如 `ok`、`bad-argument`。
- 典型错误：未知状态返回 `"unknown"`。
- 推荐调用顺序：打印错误日志时调用。

### `psa_q_rule_name(q_class)`
- 输入：`psa_q_rule_class_t`。
- 输出：类别字符串。
- 典型错误：未知类别返回 `"unknown"`。
- 推荐调用顺序：打印 `q_class` 时调用。

### `psa_q_certificate_committable(certificate)`
- 输入：证书指针。
- 输出：`bool`，表示证书是否达到可提交条件。
- 典型错误：`certificate == NULL` 或 `ready == false` 返回 `false`。
- 推荐调用顺序：录段、落日志、做结构边界判定前先调。

### `psa_q_certificate_name(certificate)`
- 输入：证书指针。
- 输出：证书名字字符串。
- 典型错误：`certificate == NULL` 或 `ready == false` 返回 `"none"`；未认证完成时可能返回 `"uncertified"`。
- 推荐调用顺序：打印证书、人类可读输出前调用。

### `psa_format_certificate(buffer, buffer_size, certificate)`
- 输入：输出缓冲区、缓冲区大小、证书。
- 输出：把证书格式化到文本缓冲区，返回字符数。
- 典型错误：`buffer == NULL` 或 `buffer_size == 0` 返回 `-1`；`certificate == NULL` 时会写入 `"none"`。
- 推荐调用顺序：`psa_core_get_certificate()` 之后调用。

### `psa_format_summary(buffer, buffer_size, summary)`
- 输入：输出缓冲区、缓冲区大小、摘要。
- 输出：把摘要格式化到文本缓冲区，返回字符数。
- 典型错误：`buffer == NULL`、`buffer_size == 0`、`summary == NULL` 返回 `-1`。
- 推荐调用顺序：`psa_core_get_summary()` 之后调用。

---

## 2. `psa_collect`

### `psa_collect_default_config(config)`
- 输入：`psa_collect_config_t*`。
- 输出：写入默认配置，默认 `invert=false`、`emit_delta=true`、`gain=1`、`offset=0`。
- 典型错误：`config == NULL` 时无动作。
- 推荐调用顺序：整个 collector 链的第一步。

### `psa_collect_init(state, config)`
- 输入：collector 状态、配置指针。
- 输出：初始化 collector 状态。
- 典型错误：`state == NULL` 时无动作；`config == NULL` 时自动退回默认配置。
- 推荐调用顺序：`psa_collect_default_config()` 之后。

### `psa_collect_push_real(state, raw_value, timestamp_ticks, input_flags, packet_out)`
- 输入：collector 状态、原始实数采样、时间戳、输入标志、可选输出包。
- 输出：返回 `PSA_STATUS_OK`，并把规范化后的采样写入内部环形缓冲；若 `packet_out != NULL` 还会同步输出一个包。
- 典型错误：`state == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；环形缓冲满时会覆盖最旧值，不返回容量错误。
- 推荐调用顺序：`psa_collect_init()` 后在采样循环里反复调用。

### `psa_collect_push_i16(state, raw_value, timestamp_ticks, input_flags, packet_out)`
- 输入：同 `psa_collect_push_real()`，只是原始值类型为 `int16_t`。
- 输出：同 `psa_collect_push_real()`。
- 典型错误：内部直接转发到 `psa_collect_push_real()`，错误口径完全一致。
- 推荐调用顺序：MCU `int16_t` ADC 采样最常用入口。

### `psa_collect_push_u16(state, raw_value, timestamp_ticks, input_flags, packet_out)`
- 输入：同上，原始值类型为 `uint16_t`。
- 输出：同 `psa_collect_push_real()`。
- 典型错误：与 `psa_collect_push_real()` 一致。
- 推荐调用顺序：MCU `uint16_t` ADC 采样常用入口。

### `psa_collect_push_i32(state, raw_value, timestamp_ticks, input_flags, packet_out)`
- 输入：同上，原始值类型为 `int32_t`。
- 输出：同 `psa_collect_push_real()`。
- 典型错误：与 `psa_collect_push_real()` 一致。
- 推荐调用顺序：上游原始数据本身就是 32 位整数时调用。

### `psa_collect_push_real_block(state, raw_values, count, first_timestamp_ticks, tick_stride, input_flags, packets_out, packets_capacity)`
- 输入：collector、实数块、块长度、首时间戳、时间戳步长、输入标志、可选输出包数组、输出包容量。
- 输出：返回实际写入 `packets_out` 的包数；内部 collector 状态会随块一起推进。
- 典型错误：`state == NULL` 或 `raw_values == NULL` 直接返回 `0`；如果 `packets_out == NULL`，内部状态照样推进，但返回值通常是 `0`，不要把它误解为“没有处理样本”。
- 推荐调用顺序：DMA、文件块、批量离线导入时调用。

### `psa_collect_push_i16_block(state, raw_values, count, first_timestamp_ticks, tick_stride, input_flags, packets_out, packets_capacity)`
- 输入：与 `psa_collect_push_real_block()` 相同，只是块类型为 `int16_t[]`。
- 输出：返回写出的包数。
- 典型错误：`state == NULL` 或 `raw_values == NULL` 返回 `0`；`packets_out == NULL` 时内部状态仍然推进。
- 推荐调用顺序：DMA 半缓冲/满缓冲场景常用。

### `psa_collect_push_u16_block(state, raw_values, count, first_timestamp_ticks, tick_stride, input_flags, packets_out, packets_capacity)`
- 输入：同上，块类型为 `uint16_t[]`。
- 输出：返回写出的包数。
- 典型错误：与 `psa_collect_push_real_block()` 一致。
- 推荐调用顺序：`uint16_t` ADC 批量输入时调用。

### `psa_collect_push_i32_block(state, raw_values, count, first_timestamp_ticks, tick_stride, input_flags, packets_out, packets_capacity)`
- 输入：同上，块类型为 `int32_t[]`。
- 输出：返回写出的包数。
- 典型错误：与 `psa_collect_push_real_block()` 一致。
- 推荐调用顺序：上游给的是 32 位整型批量数据时调用。

### `psa_collect_latest(state, packet_out)`
- 输入：collector 状态、输出包。
- 输出：成功时返回 `true` 并写出最新一包。
- 典型错误：`state == NULL`、`packet_out == NULL` 或 collector 还没有任何数据时返回 `false`。
- 推荐调用顺序：只在调试 collector、打日志、检查最新包时调用。

### `psa_collect_snapshot(state, out, max_count)`
- 输入：collector 状态、输出数组、最大复制数。
- 输出：返回实际复制出的包数。
- 典型错误：`state == NULL`、`out == NULL`、`max_count == 0` 时返回 `0`。
- 推荐调用顺序：调试、日志、离线观测 collector 环形缓冲时调用。

---

## 3. `psa_core`

### `psa_core_init(state)`
- 输入：`psa_core_state_t*`。
- 输出：初始化主链状态，并设置 `max_samples = PSA_CFG_MAX_FRONTIER`。
- 典型错误：`state == NULL` 时无动作。
- 推荐调用顺序：有 collector 时在 `psa_collect_init()` 后调用；没有 collector 时也应作为第一步。

### `psa_core_push_packet(state, packet)`
- 输入：主链状态、采样包。
- 输出：返回 `PSA_STATUS_OK` 或错误码，内部把 `packet->value` 推入主链。
- 典型错误：`packet == NULL` 直接返回 `PSA_STATUS_BAD_ARGUMENT`；`state == NULL` 的错误由内部 `psa_core_push_value()` 返回。
- 推荐调用顺序：有 collector 时优先用这个入口。

### `psa_core_push_value(state, value)`
- 输入：主链状态、已经规范化的一个实数样本。
- 输出：更新 sample、pascal、q、kernel、signature、certificate、page 等全部主链状态；成功返回 `PSA_STATUS_OK`。
- 典型错误：`state == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；样本数达到 `PSA_CFG_MAX_FRONTIER` 返回 `PSA_STATUS_CAPACITY`；内部递推更新失败时也可能返回 `PSA_STATUS_CAPACITY` 或其他状态。
- 推荐调用顺序：无 collector 时直接在采样循环里调用。

### `psa_core_get_summary(state, summary)`
- 输入：主链状态、摘要输出对象。
- 输出：写出完整 `psa_summary_t`。
- 典型错误：`state == NULL` 或 `summary == NULL` 时无动作。
- 推荐调用顺序：推入一段样本后调用，通常和 `psa_core_get_certificate()` 成对使用。

### `psa_core_get_certificate(state, certificate)`
- 输入：主链状态、证书输出对象。
- 输出：把当前正式证书拷贝出来。
- 典型错误：`state == NULL` 或 `certificate == NULL` 时无动作。
- 推荐调用顺序：`psa_core_get_summary()` 前后都可，通常一起调用。

### `psa_core_get_last_page(state, summary)`
- 输入：主链状态、页摘要输出对象。
- 输出：有页时返回 `true` 并写出最后一页摘要。
- 典型错误：`state == NULL`、`summary == NULL`、`page_count == 0` 时返回 `false`。
- 推荐调用顺序：长对象分页跟踪、在线监控分页状态时调用。

---

## 4. `psa_explain`

### `psa_explain_rule_reset(rule)`
- 输入：规则对象指针。
- 输出：规则对象清零。
- 典型错误：`rule == NULL` 时无动作。
- 推荐调用顺序：手工构造或复用规则对象前调用。

### `psa_explain_component_reset(component)`
- 输入：组件对象指针。
- 输出：组件对象清零。
- 典型错误：`component == NULL` 时无动作。
- 推荐调用顺序：手工构造或复用组件对象前调用。

### `psa_analysis_record_reset(record)`
- 输入：分析记录对象指针。
- 输出：分析记录清零。
- 典型错误：`record == NULL` 时无动作。
- 推荐调用顺序：手工复用 `psa_analysis_record_t` 前调用。

### `psa_precision_report_reset(report)`
- 输入：误差报告对象指针。
- 输出：误差报告清零，并写入当前机器精度。
- 典型错误：`report == NULL` 时无动作。
- 推荐调用顺序：手工做误差分析前调用。

### `psa_explain_pascal_workspace_reset(workspace)`
- 输入：Pascal 工作区指针。
- 输出：Pascal 工作区清零。
- 典型错误：`workspace == NULL` 时无动作。
- 推荐调用顺序：复用 Pascal 逆读回工作区前调用。

### `psa_explain_certificate_reset(certificate)`
- 输入：解释层证书对象指针。
- 输出：证书对象清零。
- 典型错误：`certificate == NULL` 时无动作。
- 推荐调用顺序：手工构造或复用证书对象前调用。

### `psa_explain_segment_record_reset(segment)`
- 输入：段记录对象指针。
- 输出：段记录清零。
- 典型错误：`segment == NULL` 时无动作。
- 推荐调用顺序：复用段对象前调用。

### `psa_explain_segment_recorder_reset(recorder)`
- 输入：录段器指针。
- 输出：录段器状态清零。
- 典型错误：`recorder == NULL` 时无动作。
- 推荐调用顺序：重新 arm 前或手工复位时调用。

### `psa_explain_stream_player_reset(player)`
- 输入：流式播放器指针。
- 输出：播放器清零。
- 典型错误：`player == NULL` 时无动作。
- 推荐调用顺序：重新 begin 前或手工复位时调用。

### `psa_explain_capture_record(state, record)`
- 输入：主链状态、分析记录输出对象。
- 输出：从当前主链抓取完整 `psa_analysis_record_t`，成功返回 `PSA_STATUS_OK`。
- 典型错误：`state == NULL` 或 `record == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；即使主链样本数为 0 也会返回 `OK`，但此时 `record->ready` 会是 `false`。
- 推荐调用顺序：`psa_core_get_summary()` 或 `psa_core_get_certificate()` 后进一步解释时调用。

### `psa_explain_select_rule(record, kind)`
- 输入：分析记录、规则种类 `SIGNAL/PASCAL/Q`。
- 输出：返回对应规则对象指针。
- 典型错误：`record == NULL` 或 `kind` 非法返回 `NULL`；它不检查 `record->ready`。
- 推荐调用顺序：`psa_explain_capture_record()` 之后调用。

### `psa_explain_replay_rule(rule, future_count, out_values, out_capacity, written_count)`
- 输入：规则、未来延拓长度、输出数组、输出容量、写出计数。
- 输出：把规则的 seed 加未来回放一起写出，成功返回 `PSA_STATUS_OK`。
- 典型错误：`rule == NULL`、`out_values == NULL`、`rule->defined == false`、`seed_count == 0` 返回 `PSA_STATUS_BAD_ARGUMENT`；`order == 0` 且 `future_count > 0` 也返回 `BAD_ARGUMENT`；容量不足返回 `PSA_STATUS_CAPACITY`。
- 推荐调用顺序：`psa_explain_select_rule()` 后直接调用。

### `psa_explain_synthesize_rules(rules, weights, rule_count, future_count, out_values, out_capacity, written_count)`
- 输入：规则数组、权重数组、规则数量、未来延拓长度、输出数组、容量、写出计数。
- 输出：把多条规则按权重叠加到一个序列里。
- 典型错误：规则数组、权重数组、输出数组为空或 `rule_count == 0` 返回 `PSA_STATUS_BAD_ARGUMENT`；任一规则未定义或种子为空也返回 `BAD_ARGUMENT`；容量不足返回 `PSA_STATUS_CAPACITY`。
- 推荐调用顺序：已有多条规则需要合成时调用。

### `psa_explain_emit_component_sequence(component, sample_count, out_values, out_capacity)`
- 输入：单个组件、输出样本数、输出数组、输出容量。
- 输出：写出单组件序列。
- 典型错误：`component == NULL` 或 `out_values == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；容量不足返回 `PSA_STATUS_CAPACITY`；组件不 active 时返回 `OK` 并输出全 0。
- 推荐调用顺序：手工组件分析或调试组件行为时调用。

### `psa_explain_synthesize_components(components, weights, component_count, sample_count, out_values, out_capacity)`
- 输入：组件数组、权重数组、组件数、样本数、输出数组、容量。
- 输出：写出组件加权叠加序列。
- 典型错误：`components == NULL`、`weights == NULL`、`out_values == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；容量不足返回 `PSA_STATUS_CAPACITY`。
- 推荐调用顺序：你已经手工拿到解释层组件并想合成时调用。

### `psa_explain_measure_error(reference, candidate, count, report)`
- 输入：参考序列、候选序列、长度、误差报告输出。
- 输出：写出 `sample_count`、`max_abs_error`、`max_scaled_error`、`machine_epsilon`。
- 典型错误：任一指针为空返回 `PSA_STATUS_BAD_ARGUMENT`。
- 推荐调用顺序：每次回放、重建、导出后都建议调用。

### `psa_explain_format_record(buffer, buffer_size, record)`
- 输入：文本缓冲区、缓冲区大小、分析记录。
- 输出：记录摘要文本，返回字符数。
- 典型错误：`buffer == NULL`、`buffer_size == 0`、`record == NULL` 返回 `-1`。
- 推荐调用顺序：`psa_explain_capture_record()` 后打印摘要时调用。

### `psa_explain_format_structural_report(buffer, buffer_size, record)`
- 输入：文本缓冲区、缓冲区大小、分析记录。
- 输出：结构报告文本，返回字符数。
- 典型错误：`buffer == NULL`、`buffer_size == 0`、`record == NULL` 返回 `-1`。
- 推荐调用顺序：查看证书、kernel、block、multiblock、page 信息时调用。

### `psa_explain_export_series(record, kind, out_values, out_capacity, written_count)`
- 输入：分析记录、序列种类、输出数组、容量、写出计数。
- 输出：导出 `sample`、`pascal`、`q`、`kernel` 之一。
- 典型错误：`record == NULL` 或 `out_values == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；`kind` 非法返回 `BAD_ARGUMENT`；容量不足返回 `PSA_STATUS_CAPACITY`。
- 推荐调用顺序：`psa_explain_capture_record()` 后最常用的导出入口。

### `psa_explain_rebuild_signal_from_pascal(record, workspace, out_values, out_capacity, written_count)`
- 输入：分析记录、显式 Pascal 工作区、输出数组、容量、写出计数。
- 输出：从 Pascal trace 精确逆读回 signal trace。
- 典型错误：`record == NULL`、`workspace == NULL`、`out_values == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；容量不足返回 `PSA_STATUS_CAPACITY`；`trace_count == 0` 时返回 `OK` 但写出数量为 0。
- 推荐调用顺序：先 `psa_explain_capture_record()`，必要时先 `psa_explain_export_series(...PASCAL...)` 理解数据，再调用本函数逆读回。

### `psa_explain_segment_recorder_arm(recorder, min_streak)`
- 输入：录段器、最小稳定次数。
- 输出：录段器 armed，成功返回 `PSA_STATUS_OK`。
- 典型错误：`recorder == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；`min_streak == 0` 时会自动修正成 `1`。
- 推荐调用顺序：没有现成记录时先 arm。

### `psa_explain_segment_recorder_arm_from_record(recorder, record, min_streak)`
- 输入：录段器、当前记录、最小稳定次数。
- 输出：先 arm，再尽量从当前记录里带上 committed 证书。
- 典型错误：`recorder == NULL` 会经由 `psa_explain_segment_recorder_arm()` 返回 `PSA_STATUS_BAD_ARGUMENT`；`record == NULL` 不报错，只是不会带入当前 committed 证书。
- 推荐调用顺序：已经有当前记录时，优先用这个入口。

### `psa_explain_segment_recorder_update(recorder, record)`
- 输入：录段器、新记录。
- 输出：推进录段状态机，必要时初始化一个段、追加一个段，或者把当前段标记 completed。
- 典型错误：`recorder == NULL`、`record == NULL`、`!recorder->armed`、`!record->ready` 返回 `PSA_STATUS_BAD_ARGUMENT`；证书暂时不可提交时返回 `PSA_STATUS_OK` 但不代表录段完成；段追加失败时可能返回 `PSA_STATUS_NUMERIC`。
- 推荐调用顺序：`arm()` 或 `arm_from_record()` 后，每来一条新记录就调用一次。

### `psa_explain_segment_recorder_completed(recorder)`
- 输入：录段器。
- 输出：是否已经完成一段。
- 典型错误：`recorder == NULL` 返回 `false`。
- 推荐调用顺序：每次 `psa_explain_segment_recorder_update()` 后检查。

### `psa_explain_segment_recorder_get_segment(recorder)`
- 输入：录段器。
- 输出：返回段对象指针。
- 典型错误：`recorder == NULL` 或段还没激活返回 `NULL`；它不要求 `completed == true`，只要 `segment.active` 就能拿到指针。
- 推荐调用顺序：通常在 `completed()` 为真后调用；如果你想观察正在增长的段，也可以在 active 后提前读。

### `psa_explain_build_certificate(record, certificate)`
- 输入：分析记录、证书输出对象。
- 输出：把记录里的正式证书拷出来。
- 典型错误：`record == NULL`、`certificate == NULL`、`record->ready == false` 返回 `PSA_STATUS_BAD_ARGUMENT`。
- 推荐调用顺序：录段前、日志打印前、外部流程要单独拿证书时调用。

### `psa_explain_stream_player_begin_rule(rule, sample_count, player)`
- 输入：规则、总样本数、播放器。
- 输出：播放器 ready，并载入规则状态。
- 典型错误：`rule == NULL`、`player == NULL`、`rule->defined == false`、`seed_count == 0` 返回 `PSA_STATUS_BAD_ARGUMENT`。
- 推荐调用顺序：直接按规则做流式播放前调用。

### `psa_explain_stream_player_begin_segment_signal(segment, player)`
- 输入：段记录、播放器。
- 输出：以 `segment.signal_rule` 为基础启动播放器。
- 典型错误：`segment == NULL`、`player == NULL`、`segment->active == false` 返回 `PSA_STATUS_BAD_ARGUMENT`；后续还会继续受 `begin_rule()` 的规则合法性约束。
- 推荐调用顺序：录段完成后播放段信号时调用。

### `psa_explain_stream_player_next(player, out_value)`
- 输入：播放器、单点输出地址。
- 输出：产出下一个样本，成功返回 `PSA_STATUS_OK`。
- 典型错误：`player == NULL`、`out_value == NULL`、`player->ready == false`、`remaining == 0` 返回 `PSA_STATUS_BAD_ARGUMENT`。
- 推荐调用顺序：`begin_rule()` 或 `begin_segment_signal()` 后循环调用，直到不再返回 `OK`。

---

## 5. `psa_direct`

### `psa_direct_component_reset(component)`
- 输入：直解组件指针。
- 输出：组件清零。
- 典型错误：`component == NULL` 时无动作。
- 推荐调用顺序：手工构造或复用组件前调用。

### `psa_direct_result_reset(result)`
- 输入：直解结果指针。
- 输出：结果对象清零，`dominant_index` 也会回到未设置状态。
- 典型错误：`result == NULL` 时无动作。
- 推荐调用顺序：手工复用 `psa_direct_result_t` 前调用。

### `psa_direct_family_reset(family)`
- 输入：直解 family 指针。
- 输出：family 清零。
- 典型错误：`family == NULL` 时无动作。
- 推荐调用顺序：手工复用 `psa_direct_family_t` 前调用。

### `psa_direct_sigma_reset(sigma)`
- 输入：sigma 对象指针。
- 输出：sigma 清零。
- 典型错误：`sigma == NULL` 时无动作。
- 推荐调用顺序：手工构造或复用 sigma 前调用。

### `psa_direct_block_reset(block)`
- 输入：block 指针。
- 输出：block 清零。
- 典型错误：`block == NULL` 时无动作。
- 推荐调用顺序：手工构造或复用 block 前调用。

### `psa_direct_block_family_reset(family)`
- 输入：block family 指针。
- 输出：block family 清零。
- 典型错误：`family == NULL` 时无动作。
- 推荐调用顺序：手工复用 block family 前调用。

### `psa_direct_high_block_reset(block)`
- 输入：高维块指针。
- 输出：高维块清零。
- 典型错误：`block == NULL` 时无动作。
- 推荐调用顺序：手工构造或复用高维块前调用。

### `psa_direct_connection_object_reset(object)`
- 输入：连接对象指针。
- 输出：连接对象清零。
- 典型错误：`object == NULL` 时无动作。
- 推荐调用顺序：手工复用连接对象前调用。

### `psa_direct_decode_rule(rule, sample_rate_hz, result)`
- 输入：一条解释层规则、采样率、结果对象。
- 输出：低阶直解结果，成功返回 `PSA_STATUS_OK`。
- 典型错误：`rule == NULL` 或 `result == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；规则未定义或阶数为 0 返回 `BAD_ARGUMENT`；当前只支持 1 阶和 2 阶，其他阶直接返回 `PSA_STATUS_NUMERIC`。
- 推荐调用顺序：已经有单条 `psa_explain_rule_t` 时调用。

### `psa_direct_decode_signal_record(record, sample_rate_hz, result)`
- 输入：分析记录、采样率、结果对象。
- 输出：从 `record->signal_rule` 生成低阶直解结果。
- 典型错误：`record == NULL` 或 `result == NULL` 返回 `BAD_ARGUMENT`；`record->ready == false`、`signal_rule` 未定义、阶数为 0、`trace_count < order` 都返回 `BAD_ARGUMENT`。
- 推荐调用顺序：`psa_explain_capture_record()` 后最常用的直解入口。

### `psa_direct_decode_rule_family(rules, rule_count, sample_rate_hz, energy_length, family)`
- 输入：规则指针数组、规则数量、采样率、能量统计长度、family 输出对象。
- 输出：多规则直解 family。
- 典型错误：`rules == NULL`、`family == NULL`、`energy_length == 0`、`energy_length > PSA_CFG_MAX_FRONTIER` 返回 `PSA_STATUS_BAD_ARGUMENT`；内部任意一条规则解码失败会直接返回对应错误；组件总数超出 `PSA_CFG_MAX_ORDER` 返回 `PSA_STATUS_CAPACITY`。
- 推荐调用顺序：多条规则并行直解时调用。

### `psa_direct_extract_sigma(record, sigma)`
- 输入：分析记录、sigma 输出对象。
- 输出：从 `q/kernel` 轨迹中提取 `sigma=(a, lambda_m)`。
- 典型错误：`record == NULL` 或 `sigma == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；`record->q_trace_count == 0` 返回 `BAD_ARGUMENT`；推得的 `start_level >= trace_count` 返回 `PSA_STATUS_NUMERIC`。
- 推荐调用顺序：`psa_explain_capture_record()` 后，需要构造 sigma 链时调用。

### `psa_direct_blocks_from_result(result, family)`
- 输入：单规则直解结果、block family 输出对象。
- 输出：把单结果拆成块族。
- 典型错误：`result == NULL` 或 `family == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；`result->ready == false` 返回 `BAD_ARGUMENT`。
- 推荐调用顺序：`psa_direct_decode_rule()` 或 `psa_direct_decode_signal_record()` 后调用。

### `psa_direct_blocks_from_family(direct_family, block_family)`
- 输入：多规则 family、block family 输出对象。
- 输出：把 family 转成块族。
- 典型错误：`direct_family == NULL` 或 `block_family == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；它不额外检查 `direct_family->ready`，因此调用方最好只在 decode 成功后使用。
- 推荐调用顺序：`psa_direct_decode_rule_family()` 后调用。

### `psa_direct_expand_sigma_connections(sigma, family)`
- 输入：sigma、block family 输出对象。
- 输出：把 sigma 中的每个 lambda 扩成标量块族。
- 典型错误：`sigma == NULL`、`family == NULL`、`sigma->ready == false` 返回 `PSA_STATUS_BAD_ARGUMENT`。
- 推荐调用顺序：只有 sigma、还没有其他 block family 时调用。

### `psa_direct_high_block_from_rule(rule, energy_length, block)`
- 输入：规则、能量统计长度、高维块输出对象。
- 输出：把高阶规则包装成 companion/high block。
- 典型错误：`rule == NULL`、`block == NULL`、规则未定义、阶数为 0、阶数超过 `PSA_CFG_MAX_ORDER` 返回 `PSA_STATUS_BAD_ARGUMENT`；`seed_count < order`、`energy_length == 0`、`energy_length > PSA_CFG_MAX_FRONTIER` 也返回 `BAD_ARGUMENT`。
- 推荐调用顺序：规则阶数大于 2，或者你要统一走高维块对象链时调用。

### `psa_direct_connection_object_from_sigma_and_family(sigma, family, object)`
- 输入：sigma、block family、连接对象输出。
- 输出：先从 block family 构造 base object，再用 sigma 抬升成完整连接对象。
- 典型错误：会直接透传 `psa_direct_connection_object_from_block_family()` 或 `psa_direct_connection_object_lift_sigma()` 的错误；常见是未 ready、参数空、容量不够。
- 推荐调用顺序：sigma 和 block family 都已准备好时调用。

### `psa_direct_connection_object_from_block_family(family, object)`
- 输入：block family、连接对象输出。
- 输出：仅从块族构造基础连接对象。
- 典型错误：`family == NULL`、`object == NULL`、`family->ready == false` 返回 `PSA_STATUS_BAD_ARGUMENT`；`block_count > PSA_CFG_MAX_ORDER` 返回 `PSA_STATUS_CAPACITY`。
- 推荐调用顺序：没有 sigma，或者想先构造 base object 再做抬升时调用。

### `psa_direct_connection_object_from_high_block(block, object)`
- 输入：高维块、连接对象输出。
- 输出：把单个高维块包装成连接对象。
- 典型错误：`block == NULL`、`object == NULL`、`block->ready == false` 返回 `PSA_STATUS_BAD_ARGUMENT`。
- 推荐调用顺序：单高维块直接进入连接对象链时调用。

### `psa_direct_connection_object_lift_sigma(sigma, base_object, object)`
- 输入：sigma、基础连接对象、目标连接对象。
- 输出：用 sigma 把 base object 扩成多 level 连接对象。
- 典型错误：`sigma == NULL`、`base_object == NULL`、`object == NULL`、任一对象未 ready 返回 `PSA_STATUS_BAD_ARGUMENT`；`base_object->part_count > PSA_CFG_MAX_ORDER` 返回 `PSA_STATUS_CAPACITY`。
- 推荐调用顺序：先 `psa_direct_connection_object_from_block_family()`，再调用本函数。

### `psa_direct_reconstruct_high_block(block, sample_count, out_values, out_capacity)`
- 输入：高维块、输出样本数、输出数组、容量。
- 输出：重建高维块对应的样本序列。
- 典型错误：`block == NULL`、`out_values == NULL`、`block->ready == false`、`dim == 0` 返回 `PSA_STATUS_BAD_ARGUMENT`；底层回放容量不足时会返回 `PSA_STATUS_CAPACITY`。
- 推荐调用顺序：`psa_direct_high_block_from_rule()` 后调用。

### `psa_direct_rebuild_connection_profile(object, out_values, out_capacity, written_count)`
- 输入：连接对象、输出数组、容量、写出计数。
- 输出：写出 `level_weight` profile。
- 典型错误：`object == NULL`、`out_values == NULL`、`object->ready == false` 返回 `PSA_STATUS_BAD_ARGUMENT`；容量不足返回 `PSA_STATUS_CAPACITY`。
- 推荐调用顺序：`psa_direct_connection_object_xxx()` 后调用。

### `psa_direct_emit_component_sequence(component, sample_count, out_values, out_capacity)`
- 输入：单组件、样本数、输出数组、容量。
- 输出：写出单组件序列。
- 典型错误：`component == NULL` 或 `out_values == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；容量不足返回 `PSA_STATUS_CAPACITY`；组件 `valid == false` 时返回 `OK` 并输出全 0。
- 推荐调用顺序：调试单个直解组件时调用。

### `psa_direct_reconstruct(result, sample_count, out_values, out_capacity)`
- 输入：单规则直解结果、样本数、输出数组、容量。
- 输出：把 result 转成 family 后整体重建。
- 典型错误：`result == NULL` 或 `out_values == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；最终还会继续受 `psa_direct_synthesize_family()` 的容量检查约束。
- 推荐调用顺序：`psa_direct_decode_rule()` 或 `psa_direct_decode_signal_record()` 后调用。

### `psa_direct_synthesize_family(family, weights, sample_count, out_values, out_capacity)`
- 输入：family、权重数组、样本数、输出数组、容量。
- 输出：family 级别的加权合成序列。
- 典型错误：`family == NULL`、`weights == NULL`、`out_values == NULL` 返回 `PSA_STATUS_BAD_ARGUMENT`；容量不足返回 `PSA_STATUS_CAPACITY`。
- 推荐调用顺序：`psa_direct_decode_rule_family()` 后调用。

### `psa_direct_format_result(buffer, buffer_size, result)`
- 输入：文本缓冲区、大小、直解结果。
- 输出：文本摘要，返回字符数。
- 典型错误：`buffer == NULL`、`buffer_size == 0`、`result == NULL` 返回 `-1`；`result->ready == false` 时会写入 `"ready=0"`。
- 推荐调用顺序：`psa_direct_decode_rule()` 或 `psa_direct_decode_signal_record()` 后打印时调用。

### `psa_direct_format_family(buffer, buffer_size, family)`
- 输入：文本缓冲区、大小、family。
- 输出：family 文本摘要。
- 典型错误：`buffer == NULL`、`buffer_size == 0`、`family == NULL` 返回 `-1`；未 ready 时写 `"ready=0"`。
- 推荐调用顺序：`psa_direct_decode_rule_family()` 后调用。

### `psa_direct_format_sigma(buffer, buffer_size, sigma)`
- 输入：文本缓冲区、大小、sigma。
- 输出：sigma 文本摘要。
- 典型错误：`buffer == NULL`、`buffer_size == 0`、`sigma == NULL` 返回 `-1`；未 ready 时写 `"ready=0"`。
- 推荐调用顺序：`psa_direct_extract_sigma()` 后调用。

### `psa_direct_format_block_family(buffer, buffer_size, family)`
- 输入：文本缓冲区、大小、block family。
- 输出：block family 文本摘要。
- 典型错误：`buffer == NULL`、`buffer_size == 0`、`family == NULL` 返回 `-1`；未 ready 时写 `"ready=0"`。
- 推荐调用顺序：`psa_direct_blocks_from_result()` 或 `psa_direct_blocks_from_family()` 后调用。

### `psa_direct_format_high_block(buffer, buffer_size, block)`
- 输入：文本缓冲区、大小、高维块。
- 输出：高维块文本摘要。
- 典型错误：`buffer == NULL`、`buffer_size == 0`、`block == NULL` 返回 `-1`；未 ready 时写 `"ready=0"`。
- 推荐调用顺序：`psa_direct_high_block_from_rule()` 后调用。

### `psa_direct_format_connection_object(buffer, buffer_size, object)`
- 输入：文本缓冲区、大小、连接对象。
- 输出：连接对象文本摘要。
- 典型错误：`buffer == NULL`、`buffer_size == 0`、`object == NULL` 返回 `-1`；未 ready 时写 `"ready=0"`。
- 推荐调用顺序：任一 `psa_direct_connection_object_xxx()` 后调用。

---

## 6. 常见错误码怎么读

### `PSA_STATUS_OK`
- 输入：不是函数，是返回码。
- 输出：表示本次调用成功。
- 典型错误：无。
- 推荐调用顺序：每次 API 返回后都先判断它。

### `PSA_STATUS_BAD_ARGUMENT`
- 输入：不是函数，是返回码。
- 输出：表示参数非法、对象未 ready、规则未定义、种子不足，或调用时机不对。
- 典型错误：最常见原因是空指针、未初始化状态、`ready == false`、`seed_count == 0`。
- 推荐调用顺序：出现时先回查“初始化有没有做”“record/rule/object 是否 ready”。

### `PSA_STATUS_CAPACITY`
- 输入：不是函数，是返回码。
- 输出：表示容量不够。
- 典型错误：最常见是输出缓冲太小、前沿容量顶到 `PSA_CFG_MAX_FRONTIER`、块/组件数超过固定上限。
- 推荐调用顺序：出现时先增大输出缓冲或检查配置上限。

### `PSA_STATUS_NUMERIC`
- 输入：不是函数，是返回码。
- 输出：表示数值条件不满足，当前对象不适合这条代数路径。
- 典型错误：常见于 `psa_direct` 低阶直解、sigma 提取或录段追加失败。
- 推荐调用顺序：出现时先确认你是不是把高阶对象误送给低阶直解入口。

---

## 7. 新手最常用的 12 个函数

1. `psa_collect_default_config()`
2. `psa_collect_init()`
3. `psa_collect_push_i16()`
4. `psa_core_init()`
5. `psa_core_push_packet()`
6. `psa_core_get_summary()`
7. `psa_core_get_certificate()`
8. `psa_explain_capture_record()`
9. `psa_explain_export_series()`
10. `psa_explain_replay_rule()`
11. `psa_explain_segment_recorder_arm_from_record()`
12. `psa_direct_decode_signal_record()`

---

## 8. 本轮全库审计结论

- 已逐个对照 `psa_base.h`、`psa_collect.h`、`psa_core.h`、`psa_explain.h`、`psa_direct.h` 的全部公开函数声明，速查表现已覆盖公开 API。
- 已复查主要实现文件 `psa_base.c`、`psa_collect.c`、`psa_core.c`、`psa_explain.c`、`psa_direct.c`，没有发现“公开头文件里有声明但实现为空壳”的情况。
- 已再次搜索 `TODO`、`FIXME`、`stub`、`placeholder`、`dead`、`zombie`、`unreachable`，当前主库源码里没有这类明显残留标记。
- 已再次搜索 `(void)xxx;` 形式的无用抑制残影，当前主库源码里没有命中。
- `psa_direct.c` 里先前清理过的几处无用局部/无用抑制残影没有回流；本轮复查时看到的同名 `static` 项里，`psa_direct_is_harmonic_of` 是“前置声明 + 正式定义”，不是重复函数体。
- 当前仍保留大量 `static` 内部辅助函数，但它们都参与主链、解释链或直解链内部实现，并不是公开空壳接口。

---

## 9. 最后提醒

- `psa_core` 的正式证书优先级高于 `psa_direct` 的解释输出。
- `psa_explain_rebuild_signal_from_pascal()` 必须给显式工作区，别忘了传 `psa_explain_pascal_workspace_t`。
- `psa_explain_segment_recorder_update()` 返回 `OK` 不代表段已经完成，必须再看 `psa_explain_segment_recorder_completed()`。
- `psa_direct_decode_rule()` 不是全阶万能入口，超过 2 阶时请优先考虑 `psa_direct_high_block_from_rule()`。
- MCU 中断主路径建议尽量只保留 `collect + core`，把 `explain + direct` 放到下游任务层。
