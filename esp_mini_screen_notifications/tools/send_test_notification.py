#!/usr/bin/env python3

import argparse
import datetime as dt
import sys
import urllib.error
import urllib.parse
import urllib.request


DEMOS = {
    "telegram": {
        "app": "Telegram",
        "sender": "Alice",
        "title": "New message",
        "body": "Hey, the notification sketch is live. Can you check the device and tell me if the card spacing feels right?",
    },
    "mail": {
        "app": "Mail",
        "sender": "Build Bot",
        "title": "CI finished successfully",
        "body": "The latest pipeline passed and the release artifacts are ready for review.",
    },
    "github": {
        "app": "GitHub",
        "sender": "Repo Watch",
        "title": "New issue comment",
        "body": "A new comment was posted on the ESP notification feature discussion.",
    },
}


def normalize_base_url(raw_url: str) -> str:
    return raw_url.rstrip("/")


def post_form(url: str, payload: dict) -> str:
    data = urllib.parse.urlencode(payload).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/x-www-form-urlencoded;charset=UTF-8"},
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
    parser.add_argument("--sender", help="Override sender/source")
    parser.add_argument("--title", help="Override title")
    parser.add_argument("--body", help="Override body text")
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
            payload = dict(DEMOS[args.demo])
            payload["updatedAt"] = args.updated_at or dt.datetime.now().strftime("%H:%M:%S")

            if args.app:
                payload["app"] = args.app
            if args.sender:
                payload["sender"] = args.sender
            if args.title:
                payload["title"] = args.title
            if args.body:
                payload["body"] = args.body

        if args.verbose:
            print(f"POST {endpoint}")
            print(payload)

        response = post_form(endpoint, payload)
        print(response)
        return 0
    except urllib.error.URLError as exc:
        print(f"Request failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
