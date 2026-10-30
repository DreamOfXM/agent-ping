import { useState } from "react";
import { ArrowRight, Check, Copy, Github, ShieldCheck, Terminal } from "lucide-react";
import { BREW_CMD, PRIVACY, REPO, READMES } from "@/data/site";
import { CmdText } from "@/components/CmdText";
import { useContent } from "@/locale";

export function Hero() {
  const { t, locale } = useContent();
  const [copied, setCopied] = useState(false);

  const copyBrew = async () => {
    try {
      await navigator.clipboard.writeText(BREW_CMD);
      setCopied(true);
      setTimeout(() => setCopied(false), 1800);
    } catch {
      setCopied(false);
    }
  };

  return (
    <section className="relative overflow-hidden pt-28 pb-20">
      {/* 背景光晕 */}
      <div aria-hidden className="pointer-events-none absolute inset-0 -z-10">
        <div className="absolute left-1/2 top-[-220px] h-[560px] w-[900px] -translate-x-1/2 rounded-full bg-[radial-gradient(ellipse_at_center,rgba(56,189,248,0.18),transparent_65%)] blur-2xl" />
        <div className="absolute right-[-160px] top-[180px] h-[420px] w-[420px] rounded-full bg-[radial-gradient(circle,rgba(167,139,250,0.14),transparent_70%)] blur-2xl" />
      </div>

      <div className="mx-auto grid max-w-6xl gap-14 px-6 lg:grid-cols-[1.15fr_0.85fr] lg:items-center">
        <div className="min-w-0">
          <span className="inline-flex items-center gap-2 rounded-full border border-border/60 bg-card/60 px-3 py-1 text-xs font-medium tracking-wide text-muted-foreground backdrop-blur">
            <span className="h-1.5 w-1.5 rounded-full bg-emerald-400" />
            {t.hero.badge}
          </span>

          <h1 className="mt-5 text-balance text-4xl font-semibold leading-[1.15] tracking-tight [word-break:keep-all] sm:text-5xl">
            {/* 「|」是断句位：中文默认哪里都能断，balance 会把「谁在等你」劈成「谁在等 / 你」；
                keep-all 让 <wbr> 成为唯一的落点。 */}
            {t.hero.h1.split("|").map((seg, i) => (
              <span key={i}>
                {i > 0 && <wbr />}
                {seg}
              </span>
            ))}
          </h1>
          <p className="mt-4 text-xl font-medium tracking-tight text-primary/90">
            {t.hero.subtitle}
          </p>
          <p className="mt-5 max-w-xl text-base leading-relaxed text-muted-foreground">
            {t.hero.tagline}
          </p>
          <p className="mt-4 inline-block rounded-lg border border-emerald-400/25 bg-emerald-400/10 px-3 py-2 text-sm font-medium text-emerald-300">
            {t.hero.promise}
          </p>

          {/* 安装命令 + CTA */}
          <div className="mt-8 flex flex-col gap-3 sm:flex-row sm:items-center">
            <button
              type="button"
              title={t.hero.brewTitle}
              onClick={copyBrew}
              className="group flex min-w-0 flex-1 items-start gap-2 rounded-xl border border-border/70 bg-card/70 px-4 py-3 text-left font-mono text-[13px] text-foreground/90 transition hover:border-sky-400/50 hover:bg-card"
            >
              <Terminal className="mt-0.5 h-4 w-4 shrink-0 text-sky-400" />
              <CmdText cmd={BREW_CMD} />
              <span className="shrink-0 text-muted-foreground transition group-hover:text-foreground">
                {copied ? <Check className="h-4 w-4 text-emerald-400" /> : <Copy className="h-4 w-4" />}
              </span>
            </button>
          </div>

          <div className="mt-4 flex flex-wrap gap-3">
            <a
              href={REPO}
              target="_blank"
              rel="noreferrer"
              className="inline-flex items-center gap-2 rounded-xl bg-primary px-5 py-3 text-sm font-semibold text-primary-foreground shadow-lg shadow-primary/20 transition hover:brightness-110 active:scale-[0.98]"
            >
              <Github className="h-4 w-4" />
              {t.hero.primaryBtn}
              <ArrowRight className="h-4 w-4" />
            </a>
            <a
              href={PRIVACY[locale] ?? PRIVACY.en}
              target="_blank"
              rel="noreferrer"
              className="inline-flex items-center gap-2 rounded-xl border border-border/70 bg-card/60 px-5 py-3 text-sm font-semibold transition hover:border-foreground/30 hover:bg-card active:scale-[0.98]"
            >
              <ShieldCheck className="h-4 w-4 text-emerald-400" />
              {t.hero.secondaryBtn}
            </a>
          </div>

          <ul className="mt-10 grid grid-cols-2 gap-x-6 gap-y-5 sm:grid-cols-4">
            {t.hero.stats.map((s) => (
              <li key={s.label}>
                <div className="text-2xl font-semibold tracking-tight text-foreground">{s.num}</div>
                <div className="mt-1 text-xs leading-snug text-muted-foreground">{s.label}</div>
              </li>
            ))}
          </ul>
        </div>

        {/* 演示动图：总览 → 额度 → 权益三屏，都由本仓库的二进制离屏渲染 */}
        <div className="relative mx-auto w-full max-w-[430px]">
          <div className="absolute -inset-4 -z-10 rounded-[2rem] bg-gradient-to-br from-sky-400/15 via-violet-400/10 to-transparent blur-xl" />
          <figure className="relative overflow-hidden rounded-2xl border border-border/60 bg-card/80 shadow-2xl shadow-black/40">
            <img
              src={t.demoGif}
              width={832}
              height={1700}
              alt={
                locale === "zh"
                  ? "Agent Ping 三屏轮播：Sessions 总览、Usage & limits 额度、Rewards 权益"
                  : "Agent Ping three-screen tour: Sessions overview, Usage & limits quota, Rewards"
              }
              className="block h-auto w-full"
              loading="eager"
              onError={(e) => {
                e.currentTarget.style.display = "none";
                e.currentTarget.parentElement?.querySelector(".demo-fallback")?.classList.remove("hidden");
              }}
            />
            <a
              href={READMES[locale] ?? READMES.zh}
              target="_blank"
              rel="noreferrer"
              className="demo-fallback hidden flex aspect-[832/1700] w-full flex-col items-center justify-center gap-2 px-8 text-center transition hover:bg-card"
            >
              <span className="text-sm font-medium">{t.hero.fallbackTitle}</span>
              <span className="text-xs text-muted-foreground">{t.hero.fallbackNote}</span>
            </a>
          </figure>
          <figcaption className="mt-3 text-center text-xs leading-relaxed text-muted-foreground">
            {t.hero.figcaption}
          </figcaption>
        </div>
      </div>
    </section>
  );
}
