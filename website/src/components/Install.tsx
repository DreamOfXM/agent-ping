import { useState } from "react";
import { Check, Copy, Info, Monitor, Apple, Terminal } from "lucide-react";
import { LINUX_BUILD, MAC_BUILD, BREW_CMD } from "@/data/site";
import { CmdText } from "@/components/CmdText";
import { useContent } from "@/locale";

const CHANNEL_ICONS = [Terminal, Apple, Monitor];
// 三个渠道都是可复制的命令；hero 那颗按钮复制的是同一串，逐字取自 README 的安装块。
const CHANNEL_CMDS = [BREW_CMD, MAC_BUILD, LINUX_BUILD];

export function Install() {
  const { t } = useContent();
  const [copied, setCopied] = useState<number | null>(null);

  const copy = async (i: number) => {
    try {
      await navigator.clipboard.writeText(CHANNEL_CMDS[i]);
      setCopied(i);
      setTimeout(() => setCopied(null), 1800);
    } catch {
      setCopied(null);
    }
  };

  return (
    <section id="install" className="scroll-mt-24 mx-auto max-w-6xl px-6 py-20">
      <header className="max-w-2xl">
        <p className="text-xs font-semibold uppercase tracking-[0.18em] text-sky-400">
          {t.install.eyebrow}
        </p>
        <h2 className="mt-3 text-3xl font-semibold tracking-tight sm:text-4xl">{t.install.title}</h2>
        <p className="mt-4 text-sm text-muted-foreground">{t.install.sub}</p>
      </header>

      <div className="mt-10 grid gap-5 md:grid-cols-3">
        {t.install.channels.map((c, i) => {
          const Icon = CHANNEL_ICONS[i];
          return (
            <div
              key={c.name}
              className="flex min-w-0 flex-col rounded-2xl border border-border/60 bg-card/50 p-6 transition hover:border-sky-400/40 hover:bg-card/80"
            >
              <div className="flex items-center gap-3">
                <div className="flex h-9 w-9 items-center justify-center rounded-lg bg-sky-400/12 text-sky-400">
                  <Icon className="h-4.5 w-4.5" />
                </div>
                <h3 className="font-semibold tracking-tight">{c.name}</h3>
              </div>
              <p className="mt-3 flex-1 text-sm leading-relaxed text-muted-foreground">{c.desc}</p>
              <button
                type="button"
                onClick={() => copy(i)}
                title={t.hero.brewTitle}
                className="mt-5 flex min-w-0 items-start gap-2 rounded-lg border border-border/70 bg-background/60 px-3 py-2.5 text-left font-mono text-xs transition hover:border-sky-400/50"
              >
                <CmdText cmd={c.action} />
                <span className="shrink-0 text-muted-foreground">
                  {copied === i ? (
                    <Check className="h-3.5 w-3.5 text-emerald-400" />
                  ) : (
                    <Copy className="h-3.5 w-3.5" />
                  )}
                </span>
              </button>
            </div>
          );
        })}
      </div>

      <p className="mt-6 flex items-start gap-2 text-xs leading-relaxed text-muted-foreground">
        <Info className="mt-0.5 h-3.5 w-3.5 shrink-0" />
        <span>{t.install.note}</span>
      </p>
    </section>
  );
}
