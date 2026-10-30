import { slashBreaks } from "@/lib/utils";

/** 命令原文的显示层：内容与剪贴板里那串逐字相同，只是允许在「/」后断行。 */
export function CmdText({ cmd }: { cmd: string }) {
  return (
    <span className="min-w-0 flex-1 whitespace-pre-wrap leading-relaxed [overflow-wrap:break-word]">
      {slashBreaks(cmd).map((seg, i) => (
        <span key={i}>
          {i > 0 && <wbr />}
          {seg}
        </span>
      ))}
    </span>
  );
}
