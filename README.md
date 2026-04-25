# 信号处理新方法

## 0.5 先看哪三个文件

- 新手总说明先看 README.md。
- 逐函数速查、输入输出和典型错误请直接看 API速查表.md。
- 如果你重点关心音频分析仪如何落地，请看 音频信号分析仪应用指导.md。

---

## 1. 这是什么

这是一个围绕“结构先行、对象先行、严格非拟合”的 C 语言信号处理库集合。

它不是传统意义上的“先做频谱拟合，再反推频率/振幅”的工具，而是把处理过程拆成 5 层：

1. `psa_base`：基础数学、签名、证书、摘要对象。
2. `psa_collect`：采样值收集、仿射映射、时间戳和增量封包。
3. `psa_core`：主链在线更新，生成 Pascal 层、q 层、kernel 层和正式结构证书。
4. `psa_explain`：把主链结果抓成记录对象，导出可输出数据，做规则回放、段录制、流式重放。
5. `psa_direct`：外置可结合直解层，只对已经剥离出来的规则对象做直接读取、块对象构造、连接对象构造和重建。

最重要的一句话：

- `psa_core` 是主链。
- `psa_explain` 是安全的下游解释层。
- `psa_direct` 不是主链判据，它只能消费已经形成的规则和证书对象，不能反过来支配主链。

如果你是新手，请先把它理解成：

- `collect` 负责“把原始采样变成规范数据包”；
- `core` 负责“在线分析并给出结构证书”；
- `explain` 负责“把结果记下来、导出来、回放出来”；
- `direct` 负责“对已经剥离好的低阶规则做直接公式读取”。

---

## 2. 当前目录结构

本目录只保留库源码、批量测量工具和这份中文 README，不包含旧 `main.c` 等原型文件。

```text
信号处理新方法/
├─ README.md
├─ API速查表.md
├─ 音频信号分析仪应用指导.md
├─ src/
│  ├─ psa_base.c
│  ├─ psa_base.h
│  ├─ psa_collect.c
│  ├─ psa_collect.h
│  ├─ psa_core.c
│  ├─ psa_core.h
│  ├─ psa_explain.c
│  ├─ psa_explain.h
│  ├─ psa_direct.c
│  └─ psa_direct.h
└─ tools/
   └─ perf_probe.c
```

---

## 3. 适合做什么

这套库适合以下任务：

- MCU 或 PC 上的在线流式结构分析。
- 从连续采样流中持续抽取结构证书。
- 在结构稳定后抓取一段对象并流式重放。
- 导出样本、Pascal、q、kernel 等可输出数据。
- 对规则对象做直接块读出、块族构造、连接对象构造、全分量读回与全局结构分解。
- 做严格的自测、精度核查、性能测量。

它不适合下面这些用途：

- 把主链建立在最小二乘、峰值搜索或传统拟合框架之上。
- 让 `psa_direct` 反过来决定主链证书。
- 把“某次显示效果好不好”当成结构判据。
- 把近零点相对误差放大后的数值，误判成主链失真。

---

## 4. 最核心的使用思路

### 4.1 一句话版

原始采样 -> `psa_collect` -> `psa_core` -> 结构证书 -> `psa_explain` 导出/录段/回放 -> `psa_direct` 对规则对象做直解

### 4.2 伪代码版

```text
初始化 collector
初始化 core

循环读取每个原始采样 raw:
    packet = collect_push(raw)
    core_push(packet.value)

    如果你只关心在线证书:
        summary = core_get_summary()
        cert = core_get_certificate()

    如果证书已经稳定且你想导出:
        record = explain_capture_record(core)
        导出 sample_trace / pascal_trace / q_trace / kernel_trace

    如果你想录一段稳定结构:
        先 arm recorder
        每次新 record 到来时 update recorder
        一旦 completed:
            取出 segment
            用 stream player 流式重放

    如果你要对规则做低阶精确直解:
        result = direct_decode_signal_record(record)
        family = direct_blocks_from_result(result)
        object = direct_connection_object_from_block_family(family)
```

### 4.3 数据流图

```text
ADC / 文件 / 仿真采样
        |
        v
psa_collect_push_xxx()
        |
        v
psa_collect_packet_t
        |
        v
psa_core_push_packet() / psa_core_push_value()
        |
        +--> psa_summary_t
        |
        +--> psa_q_certificate_t
        |
        v
psa_explain_capture_record()
        |
        +--> 规则对象 psa_explain_rule_t
        +--> trace 导出
        +--> 段录制器
        +--> 流式播放器
        |
        v
psa_direct_*()
        |
        +--> component
        +--> block family
        +--> connection object
        +--> 低阶重建
```

---

## 5. 先看一个最小可运行示例

下面是最推荐的新手起步方式。

### 5.1 最小主链示例

```c
#include "src/psa_collect.h"
#include "src/psa_core.h"
#include "src/psa_explain.h"

int main(void) {
    psa_collect_config_t cfg;
    psa_collect_state_t collector;
    psa_collect_packet_t packet;
    psa_core_state_t core;
    psa_summary_t summary;
    psa_q_certificate_t cert;

    psa_collect_default_config(&cfg);
    cfg.map.gain = 1.0 / 2048.0;
    cfg.map.offset = -1.0;

    psa_collect_init(&collector, &cfg);
    psa_core_init(&core);

    for (uint32_t i = 0; i < 256; ++i) {
        int16_t adc = 2048;
        if (psa_collect_push_i16(&collector, adc, i, 0U, &packet) != PSA_STATUS_OK) {
            return 1;
        }
        if (psa_core_push_packet(&core, &packet) != PSA_STATUS_OK) {
            return 2;
        }
    }

    psa_core_get_summary(&core, &summary);
    psa_core_get_certificate(&core, &cert);
    return 0;
}
```

### 5.2 这个示例做了什么

- 先用 `psa_collect_default_config()` 建立采样映射配置。
- 再用 `psa_collect_push_i16()` 把 `int16_t` 原始 ADC 采样变成规范包。
- 再用 `psa_core_push_packet()` 在线送入主链。
- 最后用 `psa_core_get_summary()` 和 `psa_core_get_certificate()` 读结果。

### 5.3 最重要的对象名

- `psa_collect_packet_t`：规范采样包。
- `psa_summary_t`：主链摘要。
- `psa_q_certificate_t`：正式结构证书。
- `psa_analysis_record_t`：解释层记录快照。
- `psa_explain_rule_t`：可回放规则对象。
- `psa_explain_segment_record_t`：稳定段记录对象。
- `psa_direct_result_t` / `psa_direct_block_family_t`：低阶直解结果。

---

## 6. `psa_base` 详细说明

`psa_base` 是所有其他库的共同基础。

### 6.1 你最需要认识的结构体

#### `psa_q_certificate_t`

这是整个系统里最重要的正式证书对象之一。

它包含了：

- `q_class`
- `return_period`
- `return_start_step`
- `absorb_certified`
- `finite_return_certified`
- `nonreturn_certified`
- `kernel_lift_certified`
- `sigma_radial_ready`
- `sigma_lambda[]`
- `sigma_coeff[]`
- `block_rule_active`
- `block_matrix[]`
- `block_lambda_re[]`
- `block_lambda_im[]`
- `multiblock_ready`
- `multiblock_class`
- `signature`

### 6.2 你最常用的基础函数

```c
psa_real_t psa_real_machine_epsilon(void);
psa_real_t psa_real_scaled_epsilon(psa_real_t scale);
uint64_t psa_signature_from_reals(const psa_real_t *values, size_t count, psa_real_t scale);
bool psa_q_certificate_committable(const psa_q_certificate_t *certificate);
const char *psa_q_certificate_name(const psa_q_certificate_t *certificate);
int psa_format_certificate(char *buffer, size_t buffer_size, const psa_q_certificate_t *certificate);
int psa_format_summary(char *buffer, size_t buffer_size, const psa_summary_t *summary);
```

### 6.3 什么时候该用 `psa_format_certificate()`

当你要：

- 打印串口日志
- 输出调试文本
- 保存一次结构证书快照摘要

就直接用它，不要自己拼文本。

示例：

```c
char text[256];
psa_q_certificate_t cert;
psa_core_get_certificate(&core, &cert);
psa_format_certificate(text, sizeof(text), &cert);
printf("%s\n", text);
```

---

## 7. `psa_collect` 详细说明

`psa_collect` 不是分析器，它只负责把原始采样整理成适合主链使用的数据包。

### 7.1 为什么需要它

因为原始数据往往不是直接就能送入主链的：

- 可能是 `i16`、`u16`、`i32`
- 可能带偏置
- 可能要做正负翻转
- 可能需要保留时间戳
- 可能要记录相邻样本差值 `delta`

### 7.2 最重要的配置项

```c
typedef struct {
    bool invert;
    bool emit_delta;
    psa_affine_map_t map;
} psa_collect_config_t;
```

解释：

- `invert`：是否翻转符号。
- `emit_delta`：是否输出相邻差值。
- `map.gain` / `map.offset`：把原始采样映射到目标值域。

### 7.3 最常用的采样入口

```c
psa_collect_push_real()
psa_collect_push_i16()
psa_collect_push_u16()
psa_collect_push_i32()
```

### 7.4 块输入入口

```c
psa_collect_push_real_block()
psa_collect_push_i16_block()
psa_collect_push_u16_block()
psa_collect_push_i32_block()
```

如果你来自 STM32 DMA 场景，这组 block 接口会非常顺手。

### 7.5 一段典型代码

```c
psa_collect_config_t cfg;
psa_collect_state_t collector;
psa_collect_packet_t packets[96];
int16_t raw[96];

psa_collect_default_config(&cfg);
cfg.map.gain = 1.0 / 2048.0;
cfg.map.offset = -1.0;
psa_collect_init(&collector, &cfg);

size_t emitted = psa_collect_push_i16_block(&collector,
                                            raw,
                                            96U,
                                            1000U,
                                            4U,
                                            0U,
                                            packets,
                                            96U);
```

### 7.6 使用误区

- 不要把 `collect` 当滤波器。
- 不要在 `collect` 里做拟合或频谱推断。
- 不要把 `map.gain` / `offset` 写错，否则后面整个结构尺度都会偏。

---

## 8. `psa_core` 详细说明

`psa_core` 是主链，也是整个系统里最需要认真理解的库。

### 8.1 主链负责什么

- 在线维护 signal / Pascal / q / kernel 相关历史。
- 在线更新递推对象。
- 生成摘要 `psa_summary_t`。
- 生成正式证书 `psa_q_certificate_t`。
- 维护页面摘要和签名历史。

### 8.2 主链最常用函数

```c
void psa_core_init(psa_core_state_t *state);
psa_status_t psa_core_push_packet(psa_core_state_t *state, const psa_collect_packet_t *packet);
psa_status_t psa_core_push_value(psa_core_state_t *state, psa_real_t value);
void psa_core_get_summary(const psa_core_state_t *state, psa_summary_t *summary);
void psa_core_get_certificate(const psa_core_state_t *state, psa_q_certificate_t *certificate);
bool psa_core_get_last_page(const psa_core_state_t *state, psa_page_summary_t *summary);
```

### 8.3 主链内部做了哪些事情

大致伪代码：

```text
push_value(x):
    1. 更新 signal history
    2. 更新 Pascal frontier
    3. 更新 Pascal history
    4. 更新 q history 和 kernel history
    5. 更新 signal_rule / pascal_rule / q_rule_state
    6. 更新 q mode / kernel mode
    7. 更新 signature
    8. 更新正式证书 certificate
    9. 必要时提交 page 摘要
```

### 8.4 为什么 `psa_core` 里有重标度

你会在实现里看到：

- `signal_pow2_exponent`
- `pascal_pow2_exponent`
- `psa_rescale_step()`

这不是“作弊”，而是为了防止长对象在线更新时数值爆炸或塌缩。

作用是：

- 尽量把内部值保持在适合机器精度工作的区间；
- 不破坏结构对象本身；
- 让长流式对象更稳定地在线处理。

### 8.5 一个你必须会读的证书结论

```text
q_class=finite-return
return_period=4
return_start_step=120
sigma_radial_ready=1
sigma_period=4
```

它的含义是：

- 当前对象被主链判成有限回返；
- 回返周期是 4；
- 从 `return_start_step` 开始已经进入这条结构语义；
- `sigma_radial` 相关证书已形成。

### 8.6 使用误区

- 不要把 `q_class` 单独当全部结论，应该连证书字段一起读。
- 不要只看 `summary.q_class`，更要看 `certificate`。
- 不要因为某次 `block` 没激活，就否定整条链；不是每个样例都会给你块证书。

---

## 9. `psa_explain` 详细说明

`psa_explain` 是最适合新手直接“拿结果”的库。

### 9.1 它负责什么

- 从主链抓取完整记录快照。
- 选取规则对象。
- 回放规则。
- 合成多条规则。
- 导出 sample / Pascal / q / kernel 序列。
- 从 Pascal 精确逆读回 signal。
- 录制稳定结构段。
- 流式播放器逐点重放段信号。

### 9.2 抓取记录对象

```c
psa_analysis_record_t record;
if (psa_explain_capture_record(&core, &record) != PSA_STATUS_OK) {
    return 1;
}
```

此后你就拿到了：

- `record.summary`
- `record.certificate`
- `record.signal_rule`
- `record.pascal_rule`
- `record.q_rule`
- `record.sample_trace[]`
- `record.pascal_trace[]`
- `record.q_trace[]`
- `record.kernel_trace[]`

### 9.3 导出可输出数据

这是新手最常用的接口之一。

```c
psa_real_t out[4096];
uint32_t written = 0;

psa_explain_export_series(&record,
                          PSA_EXPLAIN_SERIES_SAMPLE,
                          out,
                          4096,
                          &written);
```

你可以导出的类型：

- `PSA_EXPLAIN_SERIES_SAMPLE`
- `PSA_EXPLAIN_SERIES_PASCAL`
- `PSA_EXPLAIN_SERIES_Q`
- `PSA_EXPLAIN_SERIES_KERNEL`

### 9.4 规则回放

```c
const psa_explain_rule_t *rule = psa_explain_select_rule(&record, PSA_EXPLAIN_RULE_SIGNAL);
psa_real_t series[256];
uint32_t written = 0;
psa_explain_replay_rule(rule, 64U, series, 256U, &written);
```

含义：

- 先取到 `signal_rule`
- 再让它从当前 seed 往后回放 `future_count` 个点

### 9.5 Pascal 逆读回 signal

这个接口非常重要，但要注意它需要显式工作区。

```c
psa_explain_pascal_workspace_t workspace;
psa_real_t rebuilt[4096];
uint32_t written = 0;

psa_explain_pascal_workspace_reset(&workspace);
psa_explain_rebuild_signal_from_pascal(&record,
                                       &workspace,
                                       rebuilt,
                                       4096,
                                       &written);
```

这里的关键点：

- 它是精确逆读回路径；
- 它仍然需要整段三角工作区；
- 但工作区已经改成调用方显式提供，而不是藏在函数内部大栈数组里。

### 9.6 段录制器

这是你之前特别强调的部分：

- 收到指令后开始录制；
- 一直录到结构真正切换前；
- 记录的是稳定结构段，而不是普通签名稳定窗。

#### 推荐用法

```c
psa_explain_segment_recorder_t recorder;
psa_explain_segment_record_t const *segment;

psa_explain_segment_recorder_reset(&recorder);
psa_explain_segment_recorder_arm_from_record(&recorder, &record, 2U);

while (!psa_explain_segment_recorder_completed(&recorder)) {
    psa_analysis_record_t next_record;
    psa_explain_capture_record(&core, &next_record);
    psa_explain_segment_recorder_update(&recorder, &next_record);
}

segment = psa_explain_segment_recorder_get_segment(&recorder);
```

#### 这里的 `min_streak` 是什么

它不是随便瞎设的阈值，而是“新证书连续出现多少次才算真的切换”。

简单说：

- `1`：一变就切
- `2`：连续两次新证书才切
- `3`：更保守

### 9.7 流式播放器

如果你不想把整段一次性重建到内存里，可以一点评一点评地取：

```c
psa_explain_stream_player_t player;
psa_real_t value;

psa_explain_stream_player_begin_segment_signal(segment, &player);
while (psa_explain_stream_player_next(&player, &value) == PSA_STATUS_OK) {
    // 输出 value
}
```

这很适合：

- STM32 串口打印
- DMA 再输出
- 边播边存
- 边播边显示

---

## 10. `psa_direct` 详细说明

`psa_direct` 是“外置可结合层”，不是主链判据。

### 10.1 它负责什么

- 从低阶规则直接读出 component
- 从 component 形成 block family
- 从 sigma 和 block family 形成 connection object
- 做低阶精确重建

### 10.2 最常用入口

```c
psa_direct_decode_rule()
psa_direct_decode_signal_record()
psa_direct_decode_rule_family()
psa_direct_extract_sigma()
psa_direct_blocks_from_result()
psa_direct_blocks_from_family()
psa_direct_connection_object_from_block_family()
psa_direct_reconstruct()
psa_direct_synthesize_family()
```

### 10.3 一个典型用法

```c
psa_direct_result_t result;
psa_direct_block_family_t family;
psa_real_t rebuilt[256];

psa_direct_decode_signal_record(&record, 8000.0, &result);
psa_direct_blocks_from_result(&result, &family);
psa_direct_reconstruct(&result, 256U, rebuilt, 256U);
```

### 10.4 二阶振荡块的意义

比如 `order=2` 的余弦规则，`psa_direct` 会把它直接组织成一个二维块：

```text
[ lambda_re  -lambda_im ]
[ lambda_im   lambda_re ]
```

这就是为什么 `psa_direct_block_t` 里会有：

- `dim`
- `matrix[4]`
- `lambda_re[2]`
- `lambda_im[2]`
- `weight`
- `q_class`
- `period`

### 10.5 多块证书

如果你有多条规则对象，先解成 family，再压成 block family：

```c
const psa_explain_rule_t *rules[2] = { &rule_a, &rule_b };
psa_direct_family_t family;
psa_direct_block_family_t block_family;

psa_direct_decode_rule_family(rules, 2U, 8000.0, 128U, &family);
psa_direct_blocks_from_family(&family, &block_family);
```

### 10.6 使用误区

- 不要把 `psa_direct` 当“万能高阶谱分解器”。
- 不要让它替代主链证书。
- 不要把它输出的组件/块结果用于反向修改主链结论。

---

## 11. 如何输出其中的可输出数据

这是很多人最关心的部分，我拆开讲。

### 11.1 输出摘要文本

```c
char text[256];
psa_summary_t summary;
psa_core_get_summary(&core, &summary);
psa_format_summary(text, sizeof(text), &summary);
printf("%s\n", text);
```

适合：

- 人看
- 串口调试
- 简单日志

### 11.2 输出正式证书文本

```c
char cert_text[256];
psa_q_certificate_t cert;
psa_core_get_certificate(&core, &cert);
psa_format_certificate(cert_text, sizeof(cert_text), &cert);
printf("%s\n", cert_text);
```

### 11.3 输出 sample / Pascal / q / kernel 序列

```c
psa_real_t out[4096];
uint32_t written = 0;

psa_explain_export_series(&record, PSA_EXPLAIN_SERIES_SAMPLE, out, 4096, &written);
psa_explain_export_series(&record, PSA_EXPLAIN_SERIES_PASCAL, out, 4096, &written);
psa_explain_export_series(&record, PSA_EXPLAIN_SERIES_Q, out, 4096, &written);
psa_explain_export_series(&record, PSA_EXPLAIN_SERIES_KERNEL, out, 4096, &written);
```

### 11.4 输出规则回放数据

```c
const psa_explain_rule_t *rule;
psa_real_t out[256];
uint32_t written = 0;

rule = psa_explain_select_rule(&record, PSA_EXPLAIN_RULE_SIGNAL);
psa_explain_replay_rule(rule, 128U, out, 256U, &written);
```

### 11.5 输出段录制后的流式数据

```c
psa_explain_stream_player_t player;
psa_real_t value;

psa_explain_stream_player_begin_segment_signal(segment, &player);
while (psa_explain_stream_player_next(&player, &value) == PSA_STATUS_OK) {
    printf("%.12g\n", value);
}
```

### 11.6 输出低阶直解结果

你可以用格式化函数，也可以直接读字段：

```c
psa_direct_result_t result;
psa_direct_decode_signal_record(&record, 8000.0, &result);

for (uint32_t i = 0; i < result.component_count; ++i) {
    printf("component[%u]: freq=%.12g Hz, rho=%.12g\n",
           i,
           result.components[i].frequency_hz,
           result.components[i].rho);
}
```

### 11.7 输出批量性能/精度表

本目录提供了 `tools/perf_probe.c`。

编译示例：

```bash
gcc -std=c11 -O2 -Wall -Wextra -pedantic -I. -Isrc tools/perf_probe.c src/psa_base.c src/psa_collect.c src/psa_core.c src/psa_direct.c src/psa_explain.c -lm -o perf_probe.exe
./perf_probe.exe
```

当前实测输出表如下：

```text
machine_epsilon=2.2204460492503131e-16
batch_case_table
case|path|class|rate_unit|rate|max_abs|rms_abs|max_global_scaled|max_local_scaled|detail
absorbing|core+explain|absorbing/absorbing-tail|samples/s|1097142.857|0|0|0|0|ret=0 start=-1 sigma=0 block=unknown multiblock=unknown
finite_return|manual-cert+explain|finite-return/normalized-kernel-finite-return|samples/s|170666666.667|0|0|0|0|ret=1 start=0 sigma=1 const=1
nonreturn|manual-cert+explain|non-finite-return/non-finite-return|samples/s|170666666.667|0|0|0|0|order=1 seed=1 q=0.1 sigma=1
order2_block|direct-block|non-finite-return/block-count=1|samples/s|170666666.667|1.2434497875801753e-14|3.9063446001817286e-15|56|0|dim=2 dominant=0 period=0 harmonic=0
multiblock|direct-family|non-finite-return/blocks=2|samples/s|42666666.667|1.1657341758564144e-14|3.7339115893270501e-15|35|52.5|dim=4 period=0 dominant=0 harmonic=1
stable_segment|segment+stream|non-finite-return/non-finite-return|updates/s|305882.353|0|0|0|0|trace=128 ret=0 sigma=1
```

怎么读这张表：

- `absorbing`：吸收证书案例。
- `finite_return`：有限回返证书案例。
- `nonreturn`：不回返证书案例。
- `order2_block`：二阶振荡块直解案例。
- `multiblock`：多块证书案例。
- `stable_segment`：录段与流式回放案例。

---

## 12. 一些局部实现代码块解释

这一节专门给想“读懂源码”的新手。

### 12.1 证书对象为何这么大

局部摘录：

```c
typedef struct {
    bool ready;
    psa_q_rule_class_t q_class;
    uint64_t sigma_signature;
    uint64_t state_signature;
    uint64_t combined_signature;
    uint64_t q_signature;
    uint32_t return_period;
    int32_t return_start_step;
    bool absorb_certified;
    bool finite_return_certified;
    bool nonreturn_certified;
    bool kernel_lift_certified;
    bool sigma_radial_ready;
    psa_real_t sigma_lambda[PSA_CFG_MAX_ORDER];
    psa_real_t sigma_coeff[PSA_CFG_MAX_ORDER + 1];
    bool block_rule_active;
    psa_real_t block_matrix[4];
    psa_real_t block_lambda_re[2];
    psa_real_t block_lambda_im[2];
    bool multiblock_ready;
    uint32_t block_count;
    uint64_t signature;
} psa_q_certificate_t;
```

为什么要这样设计：

- 因为我们不想让证书语义散落在 `summary`、`record`、`direct`、日志函数里各写一份。
- 正确做法是把“结构结论本体”集中在一个正式对象里。

### 12.2 录段器为什么不看普通签名稳定窗

你会在 `psa_explain` 里看到录段器是围绕正式证书签名和提交条件工作的。

直观伪代码：

```text
如果当前证书不可提交:
    不录

如果当前还没有 committed 证书:
    等同一证书连续出现 min_streak 次
    然后从 arm 位置开始起录

如果已经有 committed 证书:
    只要证书没变就继续往段里追加
    一旦新的可提交证书稳定出现 min_streak 次
    就结束旧段
```

这样做的意义是：

- 段边界来自直接证书；
- 不是来自普通混合状态；
- 不是来自“看起来像变了”的显示层现象。

### 12.3 `perf_probe` 为什么同时输出 `max_abs` 和 `max_local_scaled`

因为只看一种误差很容易误判。

- `max_abs`：直接看绝对误差。
- `rms_abs`：看整体误差均方根。
- `max_global_scaled`：用全局参考尺度归一化。
- `max_local_scaled`：用局部样本尺度归一化。

尤其要注意：

- 在接近零的地方，机器 epsilon 归一化会被放大；
- 所以不能只盯着某一个 scaled 值，就认定算法坏了。

---

## 13. 新手推荐上手路径

如果你完全没用过这套库，建议按这个顺序：

### 第一步：只跑 `psa_collect + psa_core`

目标：先学会把数据送进去并读摘要。

你先只做这几件事：

- `psa_collect_default_config()`
- `psa_collect_init()`
- `psa_core_init()`
- `psa_collect_push_i16()` 或 `psa_core_push_value()`
- `psa_core_get_summary()`
- `psa_core_get_certificate()`

### 第二步：学会抓记录和导出序列

目标：会把结果拿出来看。

重点函数：

- `psa_explain_capture_record()`
- `psa_explain_export_series()`
- `psa_format_summary()`
- `psa_format_certificate()`

### 第三步：学会规则回放和 Pascal 逆读回

重点函数：

- `psa_explain_select_rule()`
- `psa_explain_replay_rule()`
- `psa_explain_rebuild_signal_from_pascal()`

### 第四步：学会录一段稳定结构

重点函数：

- `psa_explain_segment_recorder_arm_from_record()`
- `psa_explain_segment_recorder_update()`
- `psa_explain_segment_recorder_get_segment()`
- `psa_explain_stream_player_begin_segment_signal()`
- `psa_explain_stream_player_next()`

### 第五步：最后再碰 `psa_direct`

因为它是下游可结合层，不是主链起点。

---

## 14. 常见误区和坑

### 误区 1：以为 `psa_direct` 是主链

不是。

正确关系：

- `core` 先给结构证书；
- `explain` 抓记录；
- `direct` 再消费记录或规则对象。

### 误区 2：以为所有样例都必须激活块证书

不是。

有些样例：

- `sigma_radial` 已形成；
- 但 `block_rule_active` 不一定成立；
- 这不是错误。

### 误区 3：看到 `uncertified` 就以为主链没工作

也不对。

`uncertified` 的意思只是：

- 当前样例还没落到更强的正式提交类证书上；
- 不是说主链没算，也不是说规则对象没形成。

### 误区 4：把近零点 scaled error 误当成严重失真

一定要结合：

- `max_abs`
- `rms_abs`
- `max_global_scaled`
- `max_local_scaled`

一起看。

### 误区 5：忘了容量上限

你必须关注这些宏：

- `PSA_CFG_MAX_ORDER`
- `PSA_CFG_MAX_FRONTIER`
- `PSA_CFG_MAX_PAGES`
- `PSA_CFG_PAGE_CAPACITY`
- `PSA_COLLECT_RING_CAPACITY`

如果对象过长、前沿过大、页面太少，你可能会遇到容量限制。

### 误区 6：录段时 arm 得太早

推荐策略：

- 先等你真正看到一个可提交证书；
- 再 `arm` 段录制器；
- 不要对象还没稳定就提前录。

---

## 15. STM32H7 这类硬件如何用

### 15.1 推荐方式

- ADC + DMA 收数据
- DMA 半缓冲或全缓冲回调里批量喂给 `psa_collect_push_i16_block()`
- 再逐个包送入 `psa_core_push_packet()`
- 周期性读 `summary` 和 `certificate`
- 真正要导出详细对象时再用 `psa_explain`

### 15.2 推荐原则

- 主中断路径只做 `collect + core`
- 下游导出、录段、重放、直解尽量放到任务层或后台线程
- `psa_explain_rebuild_signal_from_pascal()` 的工作区要由调用方自己管理

### 15.3 为什么适合 MCU

因为当前设计已经注意这些点：

- 无动态分配
- 固定容量数组
- 显式工作区
- 真流式主链
- 下游与主链分离

---

## 16. 如何编译

### 16.1 Windows 下用 gcc 编译库测量工具

```bash
gcc -std=c11 -O2 -Wall -Wextra -pedantic -I. -Isrc tools/perf_probe.c src/psa_base.c src/psa_collect.c src/psa_core.c src/psa_direct.c src/psa_explain.c -lm -o perf_probe.exe
```

### 16.2 如果你自己写 `app.c`

```bash
gcc -std=c11 -O2 -Wall -Wextra -pedantic -I. -Isrc app.c src/psa_base.c src/psa_collect.c src/psa_core.c src/psa_direct.c src/psa_explain.c -lm -o app.exe
```

---

## 17. 最后给新手的建议

如果你刚开始接触这套库，请记住下面 6 句话：

1. 先用 `collect + core`，别一上来就碰 `direct`。
2. 先看 `certificate`，别只看 `summary.q_class`。
3. 先学会导出 trace，再学规则回放。
4. 录段边界看直接证书，不看普通状态签名。
5. 误差至少同时看 `max_abs` 和 `rms_abs`。
6. `psa_direct` 是下游直解层，不是主链判官。

如果你严格按这条路线走，这套库对新手也能做到比较清晰、可控、可验证。
