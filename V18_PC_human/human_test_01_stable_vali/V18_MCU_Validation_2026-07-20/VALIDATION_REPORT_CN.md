# V18 MCU 本次180 s人体静息验证报告

## 结论

本次结果证明 **V18从PC到STM32 MCU的迁移已经成功**，但还不能证明算法已经准备好输出可信的犬用HR/RR。当前可以进入“犬只探索性采集/建立数据集”阶段，不应直接进入产品判断或健康告警阶段。

## 数据与对齐

- MCU日志：`mcu_results(5).csv`，算法时刻1–175 s，共175行。
- 原始输入：`input(1).csv`，0–175.94 s，17,595个100 Hz样本，sample index连续。
- 参考：`01validation.csv`；HR 0–175 s，RR完整周期到178.55 s。
- 公平比较区间：MCU可对齐的1–175 s。
- 首个candidate：30 s；首个reportable：31 s；31–175 s持续报告，无中途掉点。

## MCU与PC V18一致性

- 同一input重放后，175个时间点、120个字段全部对齐。
- RR输出、candidate、SQI和所有状态/轴选择完全一致。
- HR决策、状态、SQI和轴选择完全一致；最大数值差仅 `8e-6 bpm`，属于浮点舍入差。

因此本次误差来自V18算法/窗口与reference之间的差异，不是MCU移植错误。

## RR表现

主评价口径是 `rr_candidate_bpm`、`rr_bpm/reportable` 对10 s causal trailing reference；瞬时breath-by-breath只辅助。

| 输出 | N | MAE | RMSE | Bias | P95 AE | Max AE | ±1 bpm | ±2 bpm |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| candidate vs 10 s ref | 146 | 0.662 | 0.831 | -0.002 | 1.706 | 2.575 | 79.5% | 97.9% |
| reportable vs 10 s ref | 145 | 0.521 | 0.647 | +0.003 | 1.353 | 1.832 | 88.3% | 100.0% |
| reportable vs instantaneous (aux) | 145 | 1.064 | 1.493 | -0.031 | 2.928 | 5.506 | 61.4% | 85.5% |

tracker把candidate MAE降低了 `21.3%`，并保持近零bias；本次RR结果理想。瞬时参考MAE较大主要是输出和reference时间尺度不同，不应作为主结论。

## HR表现

| 口径 | N | MAE | RMSE | Bias | P95 AE | Max AE | ±3 bpm | ±5 bpm |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| reportable vs instantaneous | 145 | 3.432 | 4.398 | +1.446 | 8.911 | 12.673 | 55.2% | 75.9% |
| valid-only vs instantaneous | 103 | 2.908 | 3.694 | +1.304 | 6.945 | 10.090 | 63.1% | 81.6% |
| reportable vs 18 s trailing | 145 | 2.386 | 3.071 | +1.252 | 5.589 | 9.007 | 64.8% | 91.0% |

- 最佳整体delay诊断约 `9 s`，MAE从 `3.432` 降至 `2.692 bpm`，说明18 s窗口确实造成明显时延。
- 但delay不能解释全部误差：对18 s trailing reference仍有 `9.01 bpm` 最大误差和 `+1.25 bpm`偏置。
- 39–48 s出现第一次连续严重偏差。45–46 s candidate已经落到约77 bpm，但track仍保持88.67 bpm，是明确的旧高路径释放过慢。
- 138–159 s的偏差主要发生在reference持续下降期间；candidate本身也偏高，属于长窗频谱平均、最近证据和tracker共同滞后，不只是输出限速。
- `VALID_LOW` 40点的MAE明显高于`VALID`；SQI与绝对误差Spearman相关约 `-0.38`，方向正确但校准仍不够强。

## 为什么尚未准备好可信犬用输出

1. 当前编译仍是 `VITAL_PROFILE_HUMAN_TEST=1`：RR 8–32、HR 60–120 bpm。
2. 虽然Goertzel系数表已覆盖6–260 bpm，但 `VITAL_MAX_BINS=64`。直接切换PET profile会把RR 6–90截到约6–69，把HR 80–260截到约80–143，不能直接使用。
3. Merck Veterinary Manual给出的犬静息参考为HR 70–120 bpm、RR 18–34 bpm；当前人体profile的RR上限32已经漏掉一部分正常静息范围，而未来的广频扫描必须重新设计分段/候选策略。
4. 本次几乎全是人体静息clean数据：174/175 local label为clean，估计阶段162点clean、2点mild，没有覆盖毛发、项圈松紧、犬只姿态、喘气、抓挠、走动、心率随体型差异等域迁移问题。
5. HR本身仍有两段连续偏差，且一次是高SQI/VALID下仍出现约10 bpm错误，不能仅靠reportable标志当作可信犬用结果。

## 下一步门槛

- 先实现犬用分段扫描/粗到细搜索，不能只把profile宏改成0。
- PC与MCU都做合成频率和回放测试，覆盖犬用目标频带和HR–RR倍频冲突。
- 采集同步犬用ground truth：HR优先ECG/可靠脉搏参考，RR用视频/胸带逐呼吸标注。
- 至少覆盖不同体型、毛长、项圈松紧、侧卧/趴卧/站立、睡眠、喘气和姿态切换。
- 继续把HR严重连续偏差、VALID_LOW误报率和高SQI错误作为放行指标，而不是只看总体MAE。

## 外部参考

- Merck Veterinary Manual, normal physiological values for dogs: https://www.merckvetmanual.com/multimedia/table/normal-physiological-values-for-dogs
- Merck Veterinary Manual, resting heart rates: https://www.merckvetmanual.com/multimedia/table/resting-heart-rates
