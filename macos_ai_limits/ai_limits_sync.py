#!/opt/homebrew/bin/python3

import argparse
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional


APP_DIR = Path.home() / "Library" / "Application Support" / "esp-mini-screen-ai-limits"
DEFAULT_CONFIG_PATH = APP_DIR / "config.json"
DEFAULT_CLAUDE_PROJECTS_DIR = Path.home() / ".claude" / "projects"
DEFAULT_CODEX_SESSIONS_DIR = Path.home() / ".codex" / "sessions"
DEFAULT_TIMEOUT_SECONDS = 15.0
DEFAULT_RETRIES = 3

RATE_LIMIT_RESET_RE = re.compile(r"you've hit your limit\s*[·-]\s*resets?\s+(?P<reset>.+)$", re.IGNORECASE)
PERCENT_LEFT_RE = re.compile(r"(?P<value>\d{1,3})\s*%\s*left", re.IGNORECASE)
PERCENT_USED_RE = re.compile(r"(?P<value>\d{1,3})\s*%\s*used", re.IGNORECASE)
RESET_ANY_RE = re.compile(r"resets?(?:\s+at)?\s+(?P<reset>.+)$", re.IGNORECASE)


@dataclass
class LimitMetric:
    left_percent: int
    used_percent: int
    resets_at: Optional[datetime]
    window_minutes: Optional[int]
    text: str

    def to_json(self) -> dict[str, Any]:
        return {
            "left_percent": self.left_percent,
            "used_percent": self.used_percent,
            "resets_at": self.resets_at.isoformat() if self.resets_at else None,
            "window_minutes": self.window_minutes,
            "text": self.text,
        }


@dataclass
class ProviderLimits:
    daily: Optional[LimitMetric]
    weekly: Optional[LimitMetric]
    source: Optional[str]

    def to_json(self) -> dict[str, Any]:
        return {
            "daily": self.daily.to_json() if self.daily else None,
            "weekly": self.weekly.to_json() if self.weekly else None,
            "source": self.source,
        }


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def expand_path(raw: Optional[str], default: Path) -> Path:
    if not raw:
        return default
    return Path(raw).expanduser()


def clamp_percent(value: Any) -> Optional[int]:
    try:
        percent = int(round(float(value)))
    except (TypeError, ValueError):
        return None
    if percent < 0:
        return 0
    if percent > 100:
        return 100
    return percent


def parse_timestamp(value: Any) -> Optional[datetime]:
    if value in (None, ""):
        return None

    if isinstance(value, (int, float)):
        try:
            return datetime.fromtimestamp(float(value), tz=timezone.utc).astimezone()
        except (OSError, OverflowError, ValueError):
            return None

    if isinstance(value, str):
        text = value.strip()
        if not text:
            return None
        if text.isdigit():
            try:
                return datetime.fromtimestamp(int(text), tz=timezone.utc).astimezone()
            except (OSError, OverflowError, ValueError):
                return None
        normalized = text.replace("Z", "+00:00")
        try:
            parsed = datetime.fromisoformat(normalized)
        except ValueError:
            return None
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=timezone.utc)
        return parsed.astimezone()

    return None


def parse_json_file(path: Path) -> Optional[Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return None


def short_reset_label(resets_at: Optional[datetime]) -> str:
    if not resets_at:
        return ""

    now = datetime.now().astimezone()
    local_reset = resets_at.astimezone()

    if local_reset.date() == now.date():
        return local_reset.strftime("%H:%M")

    delta_days = (local_reset.date() - now.date()).days
    if 0 < delta_days < 7:
        return local_reset.strftime("%a %H:%M")

    return local_reset.strftime("%m-%d")


def trim_text(value: str, max_len: int = 24) -> str:
    if len(value) <= max_len:
        return value
    if max_len <= 3:
        return value[:max_len]
    return value[: max_len - 3] + "..."


def build_metric(used_percent: Any, resets_at: Any, window_minutes: Any) -> Optional[LimitMetric]:
    used = clamp_percent(used_percent)
    if used is None:
        return None

    left = max(0, 100 - used)
    reset_dt = parse_timestamp(resets_at)

    try:
        window = int(window_minutes) if window_minutes not in (None, "") else None
    except (TypeError, ValueError):
        window = None

    reset_label = short_reset_label(reset_dt)
    text = f"{left}% left"
    if reset_label:
        text += f" {reset_label}"

    return LimitMetric(
        left_percent=left,
        used_percent=used,
        resets_at=reset_dt,
        window_minutes=window,
        text=trim_text(text),
    )


def load_config(path: Path) -> dict[str, Any]:
    loaded = parse_json_file(path)
    return loaded if isinstance(loaded, dict) else {}


def newest_files(root: Path, suffix: str, limit: int = 40) -> list[Path]:
    if not root.exists():
        return []

    files = [path for path in root.rglob(f"*{suffix}") if path.is_file()]
    files.sort(key=lambda item: item.stat().st_mtime, reverse=True)
    return files[:limit]


def load_latest_codex_limits(root: Path) -> Optional[ProviderLimits]:
    best_timestamp: Optional[datetime] = None
    best_payload: Optional[dict[str, Any]] = None
    best_path: Optional[Path] = None

    for path in newest_files(root, ".jsonl"):
        try:
            with path.open("r", encoding="utf-8") as handle:
                for raw_line in handle:
                    if '"rate_limits"' not in raw_line:
                        continue
                    try:
                        event = json.loads(raw_line)
                    except json.JSONDecodeError:
                        continue

                    payload = event.get("payload") or {}
                    rate_limits = payload.get("rate_limits")
                    if not isinstance(rate_limits, dict):
                        continue

                    event_time = parse_timestamp(event.get("timestamp"))
                    if event_time is None:
                        continue

                    if best_timestamp is None or event_time > best_timestamp:
                        best_timestamp = event_time
                        best_payload = rate_limits
                        best_path = path
        except OSError:
            continue

    if not best_payload:
        return None

    daily = build_metric(
        (best_payload.get("primary") or {}).get("used_percent"),
        (best_payload.get("primary") or {}).get("resets_at"),
        (best_payload.get("primary") or {}).get("window_minutes"),
    )
    weekly = build_metric(
        (best_payload.get("secondary") or {}).get("used_percent"),
        (best_payload.get("secondary") or {}).get("resets_at"),
        (best_payload.get("secondary") or {}).get("window_minutes"),
    )

    return ProviderLimits(daily=daily, weekly=weekly, source=str(best_path) if best_path else None)


def extract_message_texts(event: dict[str, Any]) -> list[str]:
    message = event.get("message")
    if not isinstance(message, dict):
        return []

    content = message.get("content")
    if isinstance(content, str):
        return [content]
    if not isinstance(content, list):
        return []

    texts: list[str] = []
    for item in content:
        if not isinstance(item, dict):
            continue
        if item.get("type") == "text" and isinstance(item.get("text"), str):
            texts.append(item["text"])
    return texts


def clean_reset_label(raw: str) -> str:
    label = raw.strip()
    label = re.sub(r"\s*\([^)]*\)\s*$", "", label)
    label = label.replace(" at ", " ")
    return label.strip()


def metric_from_text(text: str, default_window_minutes: Optional[int]) -> Optional[LimitMetric]:
    left_match = PERCENT_LEFT_RE.search(text)
    used_match = PERCENT_USED_RE.search(text)
    reset_match = RESET_ANY_RE.search(text)
    rate_limit_match = RATE_LIMIT_RESET_RE.search(text)

    left_percent: Optional[int] = None
    used_percent: Optional[int] = None

    if left_match:
        left_percent = clamp_percent(left_match.group("value"))
    elif used_match:
        used_percent = clamp_percent(used_match.group("value"))
        if used_percent is not None:
            left_percent = max(0, 100 - used_percent)
    elif rate_limit_match:
        left_percent = 0
        used_percent = 100

    if left_percent is None:
        return None

    if used_percent is None:
        used_percent = max(0, 100 - left_percent)

    raw_reset = None
    if rate_limit_match:
        raw_reset = rate_limit_match.group("reset")
    elif reset_match:
        raw_reset = reset_match.group("reset")

    reset_label = clean_reset_label(raw_reset) if raw_reset else ""
    metric_text = f"{left_percent}% left"
    if reset_label:
        metric_text += f" {reset_label}"

    return LimitMetric(
        left_percent=left_percent,
        used_percent=used_percent,
        resets_at=None,
        window_minutes=default_window_minutes,
        text=trim_text(metric_text),
    )


def classify_claude_text_metric(text: str) -> tuple[Optional[str], Optional[LimitMetric]]:
    lower = text.lower()

    if "7-day" in lower or "7 day" in lower or "current week" in lower or "weekly" in lower or "week" in lower:
        return "weekly", metric_from_text(text, 10080)

    if "5-hour" in lower or "5 hour" in lower or "5h" in lower or "daily" in lower or "today" in lower:
        return "daily", metric_from_text(text, 300)

    # Generic desktop rate-limit messages do not identify the window explicitly.
    # Prefer weekly here to avoid mislabeling the daily slot with ambiguous data.
    if "you've hit your limit" in lower:
        return "weekly", metric_from_text(text, 10080)

    return None, None


def load_claude_limits_from_transcripts(projects_root: Path) -> Optional[ProviderLimits]:
    best_daily: Optional[LimitMetric] = None
    best_weekly: Optional[LimitMetric] = None
    best_daily_time: Optional[datetime] = None
    best_weekly_time: Optional[datetime] = None
    best_source: Optional[str] = None

    for path in newest_files(projects_root, ".jsonl", limit=30):
        try:
            with path.open("r", encoding="utf-8") as handle:
                for raw_line in handle:
                    try:
                        event = json.loads(raw_line)
                    except json.JSONDecodeError:
                        continue

                    event_time = parse_timestamp(event.get("timestamp"))
                    texts = extract_message_texts(event)
                    for text in texts:
                        target, metric = classify_claude_text_metric(text)
                        if target == "daily" and metric and (best_daily_time is None or (event_time and event_time > best_daily_time)):
                            best_daily = metric
                            best_daily_time = event_time
                            best_source = str(path)
                        if target == "weekly" and metric and (best_weekly_time is None or (event_time and event_time > best_weekly_time)):
                            best_weekly = metric
                            best_weekly_time = event_time
                            best_source = str(path)
        except OSError:
            continue

    if not best_daily and not best_weekly:
        return None

    return ProviderLimits(daily=best_daily, weekly=best_weekly, source=best_source)


def build_provider_fields(prefix: str, limits: Optional[ProviderLimits]) -> dict[str, str]:
    fields: dict[str, str] = {}
    if not limits:
        fields[f"{prefix}DailyText"] = "waiting"
        fields[f"{prefix}DailyPercent"] = "0"
        fields[f"{prefix}WeeklyText"] = "waiting"
        fields[f"{prefix}WeeklyPercent"] = "0"
        return fields

    if limits.daily:
        fields[f"{prefix}DailyText"] = limits.daily.text
        fields[f"{prefix}DailyPercent"] = str(limits.daily.left_percent)
    else:
        fields[f"{prefix}DailyText"] = "waiting"
        fields[f"{prefix}DailyPercent"] = "0"

    if limits.weekly:
        fields[f"{prefix}WeeklyText"] = limits.weekly.text
        fields[f"{prefix}WeeklyPercent"] = str(limits.weekly.left_percent)
    else:
        fields[f"{prefix}WeeklyText"] = "waiting"
        fields[f"{prefix}WeeklyPercent"] = "0"

    return fields


def collect_state(config: dict[str, Any]) -> dict[str, Any]:
    codex_root = expand_path(config.get("codex_sessions_dir"), DEFAULT_CODEX_SESSIONS_DIR)
    claude_projects = expand_path(config.get("claude_projects_dir"), DEFAULT_CLAUDE_PROJECTS_DIR)

    codex = load_latest_codex_limits(codex_root)
    claude = load_claude_limits_from_transcripts(claude_projects)

    payload: dict[str, str] = {
        "updatedAt": datetime.now().astimezone().strftime("%Y-%m-%d %H:%M"),
    }
    payload.update(build_provider_fields("codex", codex))
    payload.update(build_provider_fields("claude", claude))

    return {
        "config": {
            "esp_url": config.get("esp_url"),
            "codex_sessions_dir": str(codex_root),
            "claude_projects_dir": str(claude_projects),
        },
        "providers": {
            "codex": codex.to_json() if codex else None,
            "claude": claude.to_json() if claude else None,
        },
        "payload": payload,
    }


def handle_show(args: argparse.Namespace) -> int:
    config = load_config(expand_path(args.config, DEFAULT_CONFIG_PATH))
    state = collect_state(config)
    print(json.dumps(state, indent=2, ensure_ascii=True))
    return 0


def post_form(url: str, fields: dict[str, str], timeout: float) -> tuple[int, str]:
    body = urllib.parse.urlencode(fields).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": "application/x-www-form-urlencoded; charset=utf-8"},
        method="POST",
    )

    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.getcode(), response.read().decode("utf-8", errors="replace")


def handle_push(args: argparse.Namespace) -> int:
    config = load_config(expand_path(args.config, DEFAULT_CONFIG_PATH))
    state = collect_state(config)
    payload = state["payload"]

    esp_url = args.esp_url or config.get("esp_url")
    if not esp_url:
        print(
            "Missing ESP URL. Set it in config.json or pass --esp-url http://<device-ip>/limits.",
            file=sys.stderr,
        )
        return 2

    if len(payload) <= 1:
        print("No provider data found yet. Codex or Claude sources are still empty.", file=sys.stderr)
        return 3

    if args.dry_run:
        print(json.dumps({"url": esp_url, "payload": payload}, indent=2, ensure_ascii=True))
        return 0

    timeout = float(args.timeout or config.get("timeout_seconds") or DEFAULT_TIMEOUT_SECONDS)
    retries = int(config.get("retries") or DEFAULT_RETRIES)
    last_error: Optional[str] = None

    for attempt in range(1, retries + 1):
        try:
            status_code, response_body = post_form(esp_url, payload, timeout)
            print(
                json.dumps(
                    {
                        "status_code": status_code,
                        "response": response_body,
                        "payload": payload,
                        "attempt": attempt,
                    },
                    indent=2,
                )
            )
            return 0
        except urllib.error.HTTPError as exc:
            error_body = exc.read().decode("utf-8", errors="replace")
            last_error = f"ESP HTTP error {exc.code}: {error_body}"
            break
        except urllib.error.URLError as exc:
            last_error = f"ESP request failed on attempt {attempt}/{retries}: {exc}"

    print(last_error or "ESP request failed", file=sys.stderr)
    return 5


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Collect Codex/Claude local limits and push them to ESP.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    show = subparsers.add_parser("show", help="Print parsed local limits and generated ESP payload.")
    show.add_argument("--config", help=f"Config path (default: {DEFAULT_CONFIG_PATH})")
    show.set_defaults(func=handle_show)

    push = subparsers.add_parser("push", help="Send the current payload to the ESP limits endpoint.")
    push.add_argument("--config", help=f"Config path (default: {DEFAULT_CONFIG_PATH})")
    push.add_argument("--esp-url", help="Override ESP URL, e.g. http://192.168.1.50/limits")
    push.add_argument("--timeout", type=float, help="HTTP timeout in seconds")
    push.add_argument("--dry-run", action="store_true", help="Print the request payload instead of sending it")
    push.set_defaults(func=handle_push)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
