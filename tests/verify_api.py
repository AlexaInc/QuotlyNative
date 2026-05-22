import requests
import json

url = "https://quotlytga-quotecpp.hf.space/api/generate"

tests = [
    {
        "name": "High Fidelity Premium Test",
        "payload": {
            "messages": [
                {
                    "from": {
                        "id": 12345678,
                        "first_name": "Premium User",
                        "emoji_status_custom_emoji_id": 5352552264781290450
                    },
                    "text": "Hello, this is a premium emoji E test.",
                    "entities": [
                        {
                            "offset": 31,
                            "length": 1,
                            "type": "custom_emoji",
                            "custom_emoji_id": 5352552264781290450
                        }
                    ]
                },
                {
                    "from": {
                        "id": 87654321,
                        "first_name": "Normal User"
                    },
                    "text": "Wow, looks cool! E",
                    "entities": [
                        {
                            "offset": 17,
                            "length": 1,
                            "type": "custom_emoji",
                            "custom_emoji_id": 5352552264781290450
                        }
                    ],
                    "reply_to_message": {
                        "from": { "id": 12345678, "first_name": "Premium User" },
                        "text": "Hello, this is a premium emoji E test."
                    }
                }
            ]
        }
    }
]

for t in tests:
    print(f"Running test: {t['name']}")
    try:
        r = requests.post(url, json=t['payload'], timeout=30)
        print(f"Status: {r.status_code}")
        if r.status_code == 200:
            filename = f"test_{t['name'].replace(' ', '_')}.png"
            with open(filename, "wb") as f:
                f.write(r.content)
            print(f"Saved to {filename}")
        else:
            print(f"Error: {r.text}")
    except Exception as e:
        print(f"Request failed: {e}")
