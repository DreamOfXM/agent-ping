import appIcon from "@/assets/app-icon.png";

/** 站上的品牌图形就是应用图标本体（assets/icon-1024.png 的等比缩），不另画一套。 */
export function AppMark({ className = "h-7 w-7" }: { className?: string }) {
  return (
    <img
      src={appIcon}
      alt=""
      aria-hidden="true"
      width={256}
      height={256}
      className={`${className} shrink-0 rounded-[7px] ring-1 ring-inset ring-white/12`}
    />
  );
}
