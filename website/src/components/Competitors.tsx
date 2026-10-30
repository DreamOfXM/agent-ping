import { useContent } from "@/locale";

export function Competitors() {
  const { t, locale } = useContent();
  const colon = locale === "zh" ? "：" : ": ";

  return (
    <section id="market" className="scroll-mt-24 mx-auto max-w-6xl px-6 py-20">
      <header className="max-w-3xl">
        <p className="text-xs font-semibold uppercase tracking-[0.18em] text-violet-400">
          {t.market.eyebrow}
        </p>
        <h2 className="mt-3 text-3xl font-semibold tracking-tight sm:text-4xl">{t.market.title}</h2>
        <p className="mt-4 text-base leading-relaxed text-muted-foreground">{t.market.intro}</p>
        <p className="mt-4 text-base leading-relaxed">
          <span className="text-muted-foreground">{t.market.claimLabel}</span>
          <span className="font-medium text-foreground">{t.positioning.claim}</span>
        </p>
      </header>

      {/* 表格（桌面）：形态并进项目名下面一行，列数压到 6 才放得下 */}
      <div className="mt-10 hidden overflow-x-auto rounded-2xl border border-border/60 md:block">
        <table className="w-full min-w-[900px] text-left text-sm">
          <thead className="bg-card/80 text-xs uppercase tracking-wide text-muted-foreground">
            <tr>
              <th className="px-5 py-4 font-medium">{t.market.headers[0]}</th>
              <th className="px-5 py-4 font-medium">{t.market.headers[1]}</th>
              <th className="px-5 py-4 font-medium">{t.market.headers[3]}</th>
              <th className="px-5 py-4 font-medium">{t.market.headers[4]}</th>
              <th className="px-5 py-4 font-medium">{t.market.headers[5]}</th>
              <th className="px-5 py-4 font-medium">{t.market.headers[6]}</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-border/50">
            {t.market.rows.map((c) => (
              <tr
                key={c.name}
                className={
                  c.ours
                    ? "bg-sky-400/[0.07] ring-1 ring-inset ring-sky-400/30"
                    : "bg-card/30 transition hover:bg-card/60"
                }
              >
                <td className="px-5 py-4 align-top">
                  <span className={c.ours ? "font-semibold text-sky-300" : "font-medium"}>{c.name}</span>
                  <span className="mt-1 block text-xs leading-snug text-muted-foreground">{c.kind}</span>
                </td>
                <td className="px-5 py-4 align-top whitespace-nowrap text-muted-foreground">{c.stars}</td>
                <td className="px-5 py-4 align-top text-muted-foreground">{c.tools}</td>
                <td className="px-5 py-4 align-top text-muted-foreground">{c.jump}</td>
                <td className="px-5 py-4 align-top text-muted-foreground">{c.quota}</td>
                <td className="px-5 py-4 align-top text-muted-foreground">{c.network}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      {/* 卡片（移动端） */}
      <div className="mt-10 grid gap-4 md:hidden">
        {t.market.rows.map((c) => (
          <div
            key={c.name}
            className={
              c.ours
                ? "rounded-2xl border border-sky-400/40 bg-sky-400/[0.07] p-5"
                : "rounded-2xl border border-border/60 bg-card/50 p-5"
            }
          >
            <div className="flex items-baseline justify-between gap-3">
              <h3 className={c.ours ? "font-semibold text-sky-300" : "font-semibold"}>{c.name}</h3>
              <span className="text-xs text-muted-foreground">{t.market.headers[1]} {c.stars}</span>
            </div>
            <p className="mt-1 text-xs leading-snug text-muted-foreground">{c.kind}</p>
            <dl className="mt-3 space-y-1.5 text-xs leading-relaxed text-muted-foreground">
              {[t.market.headers[3], t.market.headers[4], t.market.headers[5], t.market.headers[6]].map(
                (h, i) => (
                  <div key={h}>
                    <dt className="inline font-medium text-foreground/80">{h}{colon}</dt>
                    <dd className="inline">
                      {[c.tools, c.jump, c.quota, c.network][i]}
                    </dd>
                  </div>
                ),
              )}
            </dl>
          </div>
        ))}
      </div>

      <p className="mt-6 text-xs leading-relaxed text-muted-foreground/70">{t.market.sourceNote}</p>

      {/* 定位逻辑 */}
      <div className="mt-12 grid gap-4 sm:grid-cols-2">
        {t.positioning.logic.map(([title, body]) => (
          <div key={title} className="rounded-xl border border-border/50 bg-card/40 p-5">
            <h4 className="text-sm font-semibold text-violet-300">{title}</h4>
            <p className="mt-2 text-sm leading-relaxed text-muted-foreground">{body}</p>
          </div>
        ))}
      </div>
    </section>
  );
}
