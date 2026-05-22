import requests
import json

url = "https://quotlytga-quotecpp.hf.space/api/generate"

tests = [
    {
        "name": "Full Premium",
        "payload": {
            "messages": [
                {
                    "from": {
                        "id": 12345678,
                        "first_name": "Premium_User",
                        "emoji_status_custom_emoji_id": 5352552264781290450
                    },
                    "text": "Hello Premium!",
                    "entities": [
                        {
                            "offset": 6,
                            "length": 7,
                            "type": "custom_emoji",
                            "custom_emoji_id": 5352552264781290450
                        }
                    ]
                }
            ]
        }
    },
    {
        "name": "Multi Message Mixed",
        "payload": {
            "messages": [
                {
                    "from": {
                        "id": 12345678,
                        "first_name": "Premium_User",
                        "emoji_status_custom_emoji_id": 5352552264781290450
                    },
                    "text": "Hello, I am premium! 🌟",
                    "entities": [
                        {
                            "offset": 22,
                            "length": 2,
                            "type": "custom_emoji",
                            "custom_emoji_id": 5352552264781290450
                        }
                    ]
                },
                {
                    "from": {
                        "id": 87654321,
                        "first_name": "Normal_User"
                    },
                    "text": "And I am a normal user.",
                    "reply_to_message": {
                        "from": { "id": 12345678, "first_name": "Premium_User" },
                        "text": "Hello, I am premium! 🌟"
                    }
                }
            ]
        }
    }
]

for t in tests:
    print(f"Running test: {t['name']}")
    r = requests.post(url, json=t['payload'])
    print(f"Status: {r.status_code}")
    if r.status_code == 200:
        with open(f"test_{t['name'].replace(' ', '_')}.png", "wb") as f:
            f.write(r.content)
        print(f"Saved to test_{t['name'].replace(' ', '_')}.png")
    else:
        print(f"Error: {r.text}")
