import { clsx, type ClassValue } from "clsx";
import { twMerge } from "tailwind-merge";

export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs));
}

/** 命令只在「/」后面找断行位。
 *  overflow-wrap:anywhere 会让窄屏把 agent-ping 劈成 agen / t-ping——
 *  那是人眼抄不回终端的断法，斜杠处断才读得回去。 */
export function slashBreaks(cmd: string): string[] {
  return cmd.split(/(?<=\/)/);
}
