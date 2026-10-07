#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""折叠出路探针：面板被屏高压住、尾部会话收起来时，「收起来」必须有能点的出路。

为什么要有这个文件：真机上会话数不够，这条截断根本没触发过，于是「窗口被屏高压住、超出的行
既看不见也点不到」一直是潜在缺陷而不是已验行为。修法（每组一条「还有 N 条」＋点名换版面）
如果只用图验收，就会退化成「看了眼有那行字」——那行字在不在窗口内、点下去换不换版面、
收起来的条数对不对得上，图都答不了。

判四件事，全部从日志里的数字算（图只用来复核，不当判据）：
  A 总闸   g_panelH ≤ 窗口内容框高 —— 修前容器灌 13 条实测 1098.8 > 1054，底部整条被裁
  B 可见   最后一条真摆出来的会话行，命中框下沿 y1 ≤ 窗口高（超出＝被 ImGui 裁剪矩形切掉）
  C 可点   Σ(每条「还有 N 条」的 N) == 会话总数 − 摆出来的条数，且每条入口自己也在窗口内
  D 有出路 四颗点各自证一件事：换组（click1）、同组翻页（click2）、返回总览（clickback）、
          换到最后一组（clicklast）。每颗都要求「摆出来的那批会话」真换人＋换完仍满足 A/B/C，
          clickback 还多一条：返回后的那批必须与平铺帧逐颗相同，否则「返回」只是又一次换版面。

D 是这轮才加得起来的：AGENTPING_CLICK 原来一颗点完就退出，而 g_focus/g_page 只在内存，
第二次启动又回到平铺 ⇒「同一组连着点＝翻页」这句主张没法证伪。现在吃 ';' 分隔的序列。
另一处诚实账：click2 只在「点开后那组仍有余量」时才判——s13 夹具的排队组只有 2 条，点开后
两条全摆出，面板按规矩把入口撤了，探针却要求它继续变，于是把对的版面判成死键。要验翻页
就得灌一组多到一屏装不下的夹具（--counts await=40），否则那条记 NA。

红线：只写 --out 指的那个目录（evidence/，本来就被 .gitignore 圈住），会话数据一律来自
Tools/sni_seed_fixture.py 灌的假 HOME，不碰真 ~/.claude。

用法（容器内，仓库根）：
  python3 Tools/fold_probe.py --counts await=8,working=12,queued=4,failed=3,resting=3
退出码：0 四条全过 / 1 测了且不合格 / 2 根本没跑成（缺二进制、起不来、日志读不到数）
"""
import argparse
import os
import re
import subprocess
import sys
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
K_FOLD_ROW = 80      # src/pin_imgui_shell.cpp 的 kFoldRow：入口的 row 位＝80＋组号，85＝返回总览
GRPS = ("介入", "运行", "排队", "失败", "已结束")
# [版面] 折叠账用的是代码里的英文键，组号（row−80）要落到账上才判得了「这组还有没有可翻的」
GRP_KEYS = ("grp_await", "grp_run", "grp_queue", "grp_fail", "grp_done")
ACT_BACKOV = "收起"   # ActName(HIT_BACKOV)：点名那一屏上唯一那条回路的命中区名字
# [版面] focus=-1 page=0 预算=1054.0pt 折叠账: grp_await=8/8/0 grp_run=12/2/10 grp_queue=4/0/4! …
# 账的口径是「总数/摆出/收起」，收起那条是判据 C 的分项，叹号标的是「连组头都摆不下」那组。
RE_PANEL = re.compile(r"^\[版面\] focus=(-?\d+) page=(\d+) 预算=([\d.]+)pt 折叠账:(.*)$")
RE_HIT = re.compile(r"^\[命中区\] (\S+) r(-?\d+) (\S*) (\S*) x (-?[\d.]+)\.\.(-?[\d.]+) "
                    r"y (-?[\d.]+)\.\.(-?[\d.]+) 「(.*)」$")
RE_SHOT = re.compile(r"\[实拍\] (\S+) \((\d+)x(\d+).*?g_panelH=([\d.]+)pt，窗口内容框 (\d+)x(\d+)pt")


class Blocked(Exception):
    pass


def run_case(bin_path, fonts, seed, case):
    """起一次 xvfb 里的出图轮，返回 (日志文本, 日志路径)。case 带 tag/label/out_dir/click。"""
    out_dir = case["out_dir"]
    name = "%s-%s" % (case["label"], case["tag"])
    log = os.path.join(out_dir, "log-%s.txt" % name)
    ppm = os.path.join(out_dir, "live-%s.ppm" % name)
    env = dict(os.environ)
    # 三个语言变量整个不设而不是设空串：这一轮的裁判是「读表 → 摆行」，不能让宿主 shell 里
    # export 过的 LANG 混进来；AGENTPING_CLICK 同理，设成 "" 会让 ResolveZone 去匹配
    # 「label 为空的那些命中区」，也就是第一条会话行，等于顺手点了它。
    for k in ("LC_ALL", "LC_MESSAGES", "AGENTPING_LANG", "AGENTPING_CLICK",
              "AGENTPING_HOVER", "AGENTPING_DOWN", "AGENTPING_SHOT", "AGENTPING_HITS"):
        env.pop(k, None)
    env.update(HOME=os.path.join(seed, "home"), XDG_CONFIG_HOME=os.path.join(seed, "xdg"),
               AGENTPING_STATE_DIR=os.path.join(seed, "state"), AGENTPING_ALERT="0",
               AGENTPING_HITS="1", LANG="zh_CN.UTF-8", AGENTPING_SHOT=ppm)
    if case.get("click"):
        env["AGENTPING_CLICK"] = case["click"]
    cmd = [bin_path, fonts, "--window"]
    if not sys.platform.startswith("darwin"):
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1400x1100x24"] + cmd
    p = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=180)
    text = p.stdout.decode("utf-8", "replace")
    with open(log, "w") as f:
        f.write(text)
    if not os.path.exists(ppm) or os.path.getsize(ppm) == 0:
        raise Blocked("%s：没落盘（退出码 %d，日志 %s）" % (name, p.returncode, log))
    return text, log


def parse(text):
    """把日志拆成 (实拍读数, 命中区列表, [版面] 行列表)。读不到 [实拍] 就算没跑成。

    命中区只取最后那一段：日志里版面每变一次就重打一段（[版面] 是每段的结尾标记），
    全量拼成一个列表就会把点击前那一段的行也数进来，「入口画在窗口外」这种话就会
    被误报成面板的缺陷——实际是探针自己在混帧。
    """
    shot = RE_SHOT.search(text)
    if not shot:
        raise Blocked("日志里没有 [实拍] 行——这一轮根本没出图，不判")
    hits, panels, cur = [], [], []
    for line in text.splitlines():
        m = RE_HIT.match(line)
        if m:
            cur.append(dict(act=m.group(1), row=int(m.group(2)), agent=m.group(3),
                            native=m.group(4), x0=float(m.group(5)), x1=float(m.group(6)),
                            y0=float(m.group(7)), y1=float(m.group(8)), label=m.group(9)))
            continue
        m = RE_PANEL.match(line)
        if m:
            panels.append(dict(focus=int(m.group(1)), page=int(m.group(2)),
                               budget=float(m.group(3)), acct=m.group(4)))
            hits, cur = cur, []
    if cur:
        raise Blocked("最后一段命中区没有以 [版面] 收尾——日志被切了，不判")
    if not panels:
        raise Blocked("日志里没有 [版面] 行——AGENTPING_HITS 没生效或面板一行都没摆")
    return dict(panel_h=float(shot.group(4)), win_w=int(shot.group(5)), win_h=int(shot.group(6)),
                ppm=shot.group(1), px="%sx%s" % (shot.group(2), shot.group(3)),
                hits=hits, panels=panels)


def group_acct(panel_line):
    """折叠账那串拆成 {组键: (总, 摆, 收, 并进折行入口, 走了紧凑行)}。

    两个标记都来自 [版面] 那行的尾巴（src 里 '!'／'~'），说的都是「这组没摆组头」，
    区别只在条数记在哪：'!' 并进了最后那条跨组折行入口，'~' 记在自己的紧凑一行上。
    """
    out = {}
    for tok in panel_line["acct"].split():
        k, v = tok.split("=", 1)
        n, shown, rest = v.rstrip("!~").split("/")
        out[k] = (int(n), int(shown), int(rest), v.endswith("!"), v.endswith("~"))
    return out


def entry_targets(hits):
    """每条「还有 N 条」的 (组号, N, 命中框下沿)。组号来自 row＝kFoldRow＋组号这个约定。"""
    out = []
    for h in hits:
        if h["act"] != "展开":
            continue
        n = re.search(r"(\d+)", h["label"])
        out.append(dict(grp=h["row"] - K_FOLD_ROW, row=h["row"],
                        n=int(n.group(1)) if n else -1, y1=h["y1"]))
    return out


def folded_left(res, grp):
    """点名这一帧里，第 grp 组还收起着几条（[版面] 折叠账的第三位）。0＝整组已摆出，没有第二页。

    为什么读账而不是读命中区：入口在不在屏上取决于「这组还剩几条没摆」，而那条入口本身
    在版面换完之后就会消失。s13 那轮把「点第二次同一组」判成死键，实际是那组只有 2 条、
    点开后两条全摆出——没有第二页可翻，面板不给入口才是对的，探针要求它变了才是错的。
    """
    acct = group_acct(res["panels"][-1])
    v = acct.get(GRP_KEYS[grp])
    if v is None:
        raise Blocked("折叠账里没有第 %d 组（%s）——组序或键表跟代码分叉了" % (grp, GRP_KEYS[grp]))
    return v[2]


def backov(hits):
    """「返回总览」那颗的命中框；点名那一屏上只有这一条回路。"""
    return [h for h in hits if h["act"] == ACT_BACKOV]


def drawn(res):
    """这一帧真摆出来的会话（按命中区登记顺序）。"""
    return [h["native"] for h in res["hits"] if h["act"] == "整行"]


def frame_facts(res):
    """对一帧算 A/B/C/E 四件事，返回 (全过?, 说明)。每帧都过才算过，不只平铺那帧。"""
    acct = group_acct(res["panels"][-1])
    total = sum(v[0] for v in acct.values())
    ids = drawn(res)
    ents = entry_targets(res["hits"])
    folded = sum(e["n"] for e in ents)
    bad = []
    if res["panel_h"] > res["win_h"] + 0.5:
        bad.append("A 顶出屏：g_panelH=%.1f > 窗口 %d" % (res["panel_h"], res["win_h"]))
    last_y1 = max([h["y1"] for h in res["hits"] if h["act"] == "整行"] or [0])
    if not ids or last_y1 > res["win_h"] + 0.5:
        bad.append("B 末行 y1=%.0f vs 窗口 %d（摆 %d 行）" % (last_y1, res["win_h"], len(ids)))
    out = [e for e in ents if e["y1"] > res["win_h"] + 0.5]
    if out:
        bad.append("C 入口画在窗口外 %s" % [(e["grp"], e["y1"]) for e in out])
    if folded != total - len(ids):
        bad.append("C 条数不对：Σ N=%d 而 总数-摆出=%d-%d=%d" % (folded, total, len(ids), total - len(ids)))
    # 三组以上挤不进预算时走的是哪条出路，只有 [版面] 的标记知道：'~'＝各记自己的紧凑一行，
    # '!'＝几条并成一条没有组名的折行入口。这轮修的就是「!' 悄悄吃掉整组＋屏幕底部还空着」，
    # 不打印出来就只看得到「有入口」而看不见它把谁并没了。
    path = [(GRPS[GRP_KEYS.index(k)], v[3] and "!" or (v[4] and "~" or ""))
            for k, v in acct.items() if (v[3] or v[4])]
    # 组头摆了就欠着「下面要有会话落在它身上」。shown＝0 而 rest＞0、又没走 '~'／'!' 的组，
    # 屏面上是「组头写着 N」紧跟一条「还有 N 条」——同一个数报两遍、中间一行都没有，
    # 而 A/B/C 三条照样全过（账是平的，空转的是版面）。10-08 这形状靠读裁切图才撞见，
    # 所以把它写成判据挂进每一帧，不许下一轮再靠「恰好有人去看了眼图」。
    empty_hdr = [(GRPS[GRP_KEYS.index(k)], v[0]) for k, v in acct.items()
                 if v[1] == 0 and v[2] > 0 and not (v[3] or v[4])]
    if empty_hdr:
        bad.append("E 组头底下零行 %s" % empty_hdr)
    detail = "会话 %d／摆出 %d／入口 %s（Σ=%d）／g_panelH %.1f ≤ 窗口 %d／末行 y1 %.0f／预算 %.0f 用了 %.0f（空 %d）／没摆组头%s" % \
             (total, len(ids), [(GRPS[e["grp"]], e["n"]) for e in ents], folded,
              res["panel_h"], res["win_h"], last_y1, res["panels"][-1]["budget"],
              res["panel_h"], int(res["panels"][-1]["budget"] - res["panel_h"]),
              path if path else "（无）")
    return not bad, detail + ("" if not bad else " ← " + "；".join(bad))


def judge(base, cases):
    """A–E 八条逐条算成话，返回 ([(名, PASS/FAIL/NA, 读数)], 会话总数, 摆出条数)。

    cases 是 [(tag, 解析结果)]，base 是没点过的那一帧。NA 不等于 PASS：一条都没收起时
    「点入口」这件事根本没发生，报成通过就是替面板编证据。
    """
    rows = []
    total = sum(v[0] for v in group_acct(base["panels"][0]).values())
    ids = drawn(base)
    ents = entry_targets(base["hits"])

    ok_a = base["panel_h"] <= base["win_h"] + 0.5
    rows.append(("A 总闸 g_panelH ≤ 窗口高", "PASS" if ok_a else "FAIL",
                 "g_panelH=%.1fpt vs 窗口 %dpt（差 %+.1f）" % (base["panel_h"], base["win_h"],
                                                              base["panel_h"] - base["win_h"])))
    last = max([h for h in base["hits"] if h["act"] == "整行"], key=lambda h: h["y1"], default=None)
    if last is None:
        rows.append(("B 最后一行可见", "FAIL", "一帧会话行都没摆，判不了"))
    else:
        ok = last["y1"] <= base["win_h"] + 0.5
        rows.append(("B 最后一行可见", "PASS" if ok else "FAIL",
                     "最后一条 %s/%s 命中框 y %.0f..%.0f ≤ 窗口高 %d" %
                     (last["agent"], last["native"], last["y0"], last["y1"], base["win_h"])))
    folded = sum(e["n"] for e in ents)
    out_of_win = [e for e in ents if e["y1"] > base["win_h"] + 0.5]
    if not ents and folded == total - len(ids):
        rows.append(("C 收起来的可数且入口在屏内", "NA",
                     "会话 %d 条全摆出（%d 条），一条都没收起 ⇒ 没有入口可核" % (total, len(ids))))
    else:
        ok = folded == total - len(ids) and not out_of_win
        rows.append(("C 收起来的可数且入口在屏内", "PASS" if ok else "FAIL",
                     "会话 %d 条：摆出 %d，入口 %d 条各 (组,N)=%s，Σ N=%d（应 = %d-%d = %d）%s" %
                     (total, len(ids), len(ents), [(GRPS[e["grp"]], e["n"]) for e in ents], folded,
                      total, len(ids), total - len(ids),
                      "" if not out_of_win else "｜有入口画在窗口外：%s" % out_of_win)))

    # D 有动静：每颗点下去都得让「摆出来的那批会话」真的换人，且换完仍然满足 A/B/C
    #    （只看 focus/page 那两个数不够——数字变了但行的集合没变，用户看到的还是同一批）
    #    四颗各证一件事：换组、同组翻页、返回总览、换到最后一组。
    by_tag = dict(cases)

    def frame_row(tag, ref, want):
        res = by_tag.get(tag)
        if res is None:
            return ("D %s（%s）" % (tag, want), "NA", "这一轮压根没点")
        before, after = by_tag[ref]["panels"][-1], res["panels"][-1]
        fits, detail = frame_facts(res)
        moved = set(drawn(res)) != set(drawn(by_tag[ref]))
        return ("D %s（%s）" % (tag, want), "PASS" if (fits and moved) else "FAIL",
                "focus %s→%s page %s→%s｜摆出 %s→%s%s｜%s" %
                (before["focus"], after["focus"], before["page"], after["page"],
                 len(drawn(by_tag[ref])), len(drawn(res)),
                 "" if moved else "（同一批，等于没动）", detail))

    if not ents:
        # 屏装得下：没有入口，这四件事都没发生。记 NA 不记 PASS——替面板把「没测」写成「过了」
        # 是这类探针最容易犯的假账。
        for tag, want in (("click1", "换组"), ("click2", "同组翻页"),
                          ("clickback", "返回总览"), ("clicklast", "换到另一组")):
            rows.append(("D %s（%s）" % (tag, want), "NA",
                         "会话 %d 条全摆出，没有入口可点" % total))
    else:
        first, last = ents[0]["grp"], ents[-1]["grp"]
        rows.append(frame_row("click1", "flat", "换组"))
        r = by_tag.get("click2")
        if r is None:
            rows.append(("D click2（同组翻页）", "NA", "这一轮压根没点"))
        else:
            left = folded_left(by_tag["click1"], first)
            if left <= 0:
                rows.append(("D click2（同组翻页）", "NA",
                             "展开%s 之后这组已经全部摆出（折叠账收起＝%d），本夹具没有第二页可翻 ⇒ "
                             "「连着点＝翻页」这句在这轮无从验证。要测它得灌一组条数多到一屏装不下的夹具。"
                             % (GRPS[first], left)))
            else:
                rows.append(frame_row("click2", "click1", "同组翻页"))
        # 返回总览：点名那一屏上唯一的回路。判的不是「有没有动静」而是「回不回得来」——
        # 摆出的那批必须与平铺帧逐颗相同，否则「返回」只是又一次换版面。
        r = by_tag.get("clickback")
        if r is None:
            rows.append(("D clickback（返回总览）", "NA", "这一轮压根没点"))
        elif not backov(by_tag["click1"]["hits"]):
            rows.append(("D clickback（返回总览）", "FAIL",
                         "展开%s 之后命中区表里没有一条「%s」——版面换给一组就没有回来的路" %
                         (GRPS[first], ACT_BACKOV)))
        else:
            fits, detail = frame_facts(r)
            same = set(drawn(r)) == set(drawn(base))
            ok = fits and r["panels"][-1]["focus"] == -1 and same
            rows.append(("D clickback（返回总览）", "PASS" if ok else "FAIL",
                         "focus %s→%s page %s→%s｜摆出 %s→%s，与平铺帧同一批=%s｜%s" %
                         (by_tag["click1"]["panels"][-1]["focus"], r["panels"][-1]["focus"],
                          by_tag["click1"]["panels"][-1]["page"], r["panels"][-1]["page"],
                          len(drawn(by_tag["click1"])), len(drawn(r)), same, detail)))
        rows.append(frame_row("clicklast", "flat", "换到另一组"))
    # E 组头底下至少一行会话——挂在每一帧上（平铺＋四颗点），不只平铺那帧。
    # 这条是 10-08 读裁切图撞出来的：三组的组头各配一条「还有 N 条」，中间一行都没有，
    # 而 A/B/C 三条全过。判据只挂得住数得出的东西，挂不住「这块版面在空转」，所以补这一条。
    offenders, folded_any = [], False
    for tag, res in cases:      # cases 里第一项就是 ("flat", base)，别再前置一遍否则每帧数两次
        for k, v in group_acct(res["panels"][-1]).items():
            if v[2] > 0:
                folded_any = True
            if v[1] == 0 and v[2] > 0 and not (v[3] or v[4]):
                offenders.append((tag, GRPS[GRP_KEYS.index(k)], v[0]))
    if not folded_any:
        rows.append(("E 组头底下至少一行会话", "NA", "每一帧每组都全摆出了（rest 全 0），没有空转的组头"))
    elif offenders:
        rows.append(("E 组头底下至少一行会话", "FAIL",
                     "%d 处：组头报了条数、下面一行会话都没有，紧跟的入口又是同一个数 %s" %
                     (len(offenders), offenders)))
    else:
        rows.append(("E 组头底下至少一行会话", "PASS",
                     "%d 帧逐帧核：凡是 rest＞0 的组要么真摆出至少一行，要么退成带组名的紧凑一行（'~'）"
                     % len(cases)))
    return rows, total, len(ids)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(
        ROOT, "build/pin_imgui_shell" if sys.platform.startswith("darwin")
        else "build-linux/pin_imgui_shell"))
    ap.add_argument("--fonts", default=os.path.join(ROOT, "fonts"))
    ap.add_argument("--out", default=os.path.join(ROOT, "evidence/2026-10-08-fold"))
    ap.add_argument("--counts", default="await=8,working=12,queued=4,failed=3,resting=3",
                    help="灌进假 HOME 的档位条数（传给 sni_seed_fixture.py 的 --counts）")
    ap.add_argument("--label", default="s30",
                    help="产物名前缀：换一档夹具就换一个标签，否则后一轮会把前一轮的图和日志盖掉")
    a = ap.parse_args()

    for p, what in ((a.bin, "二进制"), (a.fonts, "字体目录")):
        if not os.path.exists(p):
            print("没跑成：%s 不存在 %s" % (what, p))
            return 2
    os.makedirs(a.out, exist_ok=True)
    seed = os.path.join(a.out, ".seed-home")
    shutil.rmtree(seed, ignore_errors=True)
    for sub in ("home", "xdg", "state"):
        os.makedirs(os.path.join(seed, sub), exist_ok=True)
    fx = os.path.join(ROOT, "Tools/sni_seed_fixture.py")
    sp = subprocess.run([sys.executable, fx, "--home", os.path.join(seed, "home"),
                         "--counts", a.counts], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    seed_text = sp.stdout.decode("utf-8", "replace")
    with open(os.path.join(seed, "seed.txt"), "w") as f:
        f.write(seed_text)
    if sp.returncode:
        print(seed_text)
        print("没跑成：夹具没灌进去")
        return 2

    try:
        text, log = run_case(a.bin, a.fonts, seed,
                             dict(tag="flat", out_dir=a.out, label=a.label))
        base = parse(text)
        # 点击按「组号」钉而不是坐标：版面一变入口就挪位置，坐标序列的第二颗会钉歪
        # （这轮第一版就是这么把 click2 判成死键的）。组号是语义地址，跟着版面走。
        ents = entry_targets(base["hits"])
        cases = [("flat", base)]
        if ents:
            first, last = ents[0]["grp"], ents[-1]["grp"]
            plan = [("click1", "展开%d" % first),
                    ("click2", "展开%d;展开%d" % (first, first)),
                    ("clickback", "展开%d;收起" % first),
                    ("clicklast", "展开%d" % last)]
        else:
            # 一条都没收起（会话少到屏装得下）：没有入口就没有「点下去有没有动静」这件事，
            # 那一条记 NA，不许当成通过——A/B/C 仍然要判（屏装得下时更要判它没被误伤）。
            plan = []
        for tag, click in plan:
            t, _ = run_case(a.bin, a.fonts, seed,
                            dict(tag=tag, out_dir=a.out, label=a.label, click=click))
            cases.append((tag, parse(t)))
        rows, total, drawn = judge(base, cases)
    except Blocked as e:
        print("没跑成：%s" % e)
        return 2
    print("夹具：%s" % seed_text.strip().splitlines()[-1])
    print("平铺帧：ppm=%s %s，窗口内容框 %dx%dpt，会话 %d 条 / 摆出 %d 条 / 入口 %d 条" %
          (base["ppm"], base["px"], base["win_w"], base["win_h"], total, drawn, len(entry_targets(base["hits"]))))
    fails = nas = 0
    for name, st, detail in rows:
        print("%-34s %s  %s" % (name, st, detail))
        if st == "FAIL":
            fails += 1
        elif st != "PASS":
            nas += 1
    print("—— %d 判：%d 过 / %d 不合格 / %d NA（NA 不算过）" % (len(rows), len(rows) - fails - nas, fails, nas))
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
