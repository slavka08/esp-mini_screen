#!/usr/bin/env python3

import argparse
import datetime as dt
import json
import sys
import urllib.error
import urllib.request


DEMOS = {
    "telegram": {
        "appName": "Telegram",
        "bundleId": "com.tdesktop.telegram",
        "sender": "Alice",
        "title": "New message",
        "subtitle": "Friends",
        "body": "Hey, the notification sketch is live. Can you check the device and tell me if the card spacing feels right?",
        "accent": "#2AABEE",
        "durationSeconds": 5,
    },
    "mail": {
        "appName": "Mail",
        "bundleId": "com.apple.mail",
        "sender": "Build Bot",
        "title": "CI finished successfully",
        "subtitle": "Release pipeline",
        "body": "The latest pipeline passed and the release artifacts are ready for review.",
        "accent": "#FFB020",
        "durationSeconds": 8,
    },
    "github": {
        "appName": "GitHub",
        "bundleId": "com.github.GitHubClient",
        "sender": "Repo Watch",
        "title": "New issue comment",
        "subtitle": "esp-mini_screen",
        "body": "A new comment was posted on the ESP notification feature discussion.",
        "accent": "#8A8F98",
        "durationSeconds": 6,
    },
}


def normalize_base_url(raw_url: str) -> str:
    return raw_url.rstrip("/")


def post_json(url: str, payload: dict) -> str:
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json;charset=UTF-8"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=5.0) as response:
        return response.read().decode("utf-8", errors="replace")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send a test notification to esp_mini_screen_notifications."
    )
    parser.add_argument(
        "--device-url",
        required=True,
        help="ESP base URL, for example http://192.168.1.50 or http://192.168.1.50/notify",
    )
    parser.add_argument(
        "--demo",
        choices=sorted(DEMOS.keys()),
        default="telegram",
        help="Built-in demo notification (default: telegram)",
    )
    parser.add_argument("--app", help="Override app name")
    parser.add_argument("--bundle-id", help="Override bundle identifier")
    parser.add_argument("--sender", help="Override sender/source")
    parser.add_argument("--title", help="Override title")
    parser.add_argument("--subtitle", help="Override subtitle/chat/channel")
    parser.add_argument("--body", help="Override body text")
    parser.add_argument("--accent", help="Override accent color, for example #2AABEE")
    parser.add_argument("--foreground", default="#FFFFFF", help="Override text color, for example #FFFFFF")
    parser.add_argument("--duration-seconds", type=int, help="Override visible duration in seconds (1..30)")
    parser.add_argument(
        "--updated-at",
        help="Override notification timestamp text (default: current local time)",
    )
    parser.add_argument(
        "--clear",
        action="store_true",
        help="Clear the current notification instead of sending a new one",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print the exact endpoint and payload before sending",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    base_url = normalize_base_url(args.device_url)

    try:
        if args.clear:
            endpoint = base_url if base_url.endswith("/clear") else f"{base_url}/clear"
            payload = {}
        else:
            endpoint = base_url if base_url.endswith("/notify") else f"{base_url}/notify"
            demo = dict(DEMOS[args.demo])

            if args.app:
                demo["appName"] = args.app
            if args.bundle_id:
                demo["bundleId"] = args.bundle_id
            if args.sender:
                demo["sender"] = args.sender
            if args.title:
                demo["title"] = args.title
            if args.subtitle:
                demo["subtitle"] = args.subtitle
            if args.body:
                demo["body"] = args.body
            if args.accent:
                demo["accent"] = args.accent
            if args.duration_seconds is not None:
                demo["durationSeconds"] = max(1, min(30, args.duration_seconds))

            payload = {
                "version": 2,
                "source": {
                    "appName": demo["appName"],
                    "bundleId": demo["bundleId"],
                    "sender": demo["sender"],
                },
                "content": {
                    "title": demo["title"],
                    "subtitle": demo["subtitle"],
                    "body": demo["body"],
                    "time": args.updated_at or dt.datetime.now().strftime("%H:%M:%S"),
                },
                "style": {
                    "accent": demo["accent"],
                    "foreground": args.foreground,
                    "durationMs": max(1, min(30, demo["durationSeconds"])) * 1000,
                },
            }

        if args.verbose:
            print(f"POST {endpoint}")
            print(payload)

        response = post_json(endpoint, payload)
        print(response)
        return 0
    except urllib.error.URLError as exc:
        print(f"Request failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
