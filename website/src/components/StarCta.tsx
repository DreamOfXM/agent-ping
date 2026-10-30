import { Github, MessageSquareQuote, Star } from "lucide-react";
import { ISSUES, REPO } from "@/data/site";
import { useContent } from "@/locale";

export function StarCta() {
  const { t } = useContent();

  return (
    <section id="star" className="scroll-mt-24 mx-auto max-w-4xl px-6 pb-24">
      <div className="reveal-up relative overflow-hidden rounded-3xl border border-border/60 bg-card/50 px-8 py-14 text-center">
        {/* 背景光晕，与 Hero 同一套 */}
        <div aria-hidden className="pointer-events-none absolute inset-0 -z-10">
          <div className="absolute left-1/2 top-[-140px] h-[320px] w-[620px] -translate-x-1/2 rounded-full bg-[radial-gradient(ellipse_at_center,rgba(56,189,248,0.14),transparent_65%)] blur-2xl" />
        </div>

        <p className="text-xs font-semibold uppercase tracking-[0.18em] text-sky-400">
          {t.cta.eyebrow}
        </p>
        <h2 className="mt-3 text-3xl font-semibold tracking-tight sm:text-4xl">
          {t.cta.title}
        </h2>
        <p className="mx-auto mt-4 max-w-xl text-sm leading-relaxed text-muted-foreground">
          {t.cta.body}
        </p>

        <div className="mt-8 flex flex-wrap items-center justify-center gap-3">
          <a
            href={REPO}
            target="_blank"
            rel="noreferrer"
            className="inline-flex items-center gap-2 rounded-xl bg-primary px-5 py-3 text-sm font-semibold text-primary-foreground shadow-lg shadow-primary/20 transition hover:brightness-110 active:scale-[0.98]"
          >
            <Star className="h-4 w-4" />
            {t.cta.starBtn}
          </a>
          <a
            href={ISSUES}
            target="_blank"
            rel="noreferrer"
            className="inline-flex items-center gap-2 rounded-xl border border-border/70 bg-card/60 px-5 py-3 text-sm font-semibold transition hover:border-foreground/30 hover:bg-card active:scale-[0.98]"
          >
            <Github className="h-4 w-4" />
            {t.cta.issueBtn}
          </a>
        </div>

        <div className="mt-10 border-t border-border/50 pt-6">
          <p className="mx-auto max-w-xl text-sm leading-relaxed text-muted-foreground">
            {t.cta.feedbackLead}
          </p>
          <p className="mx-auto mt-4 flex max-w-xl items-start justify-center gap-2 text-xs leading-relaxed text-muted-foreground/70">
            <MessageSquareQuote className="mt-0.5 h-3.5 w-3.5 shrink-0" />
            <span>{t.cta.issueNote}</span>
          </p>
        </div>
      </div>
    </section>
  );
}
