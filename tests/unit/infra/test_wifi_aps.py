"""Offline checks for infra/wifi_aps.py: selection order, search, band filter."""
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from infra import wifi_aps  # noqa: E402

PROFILES = [
    {"name": "office-2g", "location": "office", "ssid": "LAB_2G", "password": "p1",
     "band": "2g", "subnet": "192.168.50.0/24", "tags": ["bench"]},
    {"name": "office-5g", "location": "office", "ssid": "LAB_5G", "password": "p2",
     "band": "5g", "subnet": "192.168.50.0/24"},
    {"name": "home", "location": "home", "ssid": "HOME", "password": "p3",
     "band": "dual", "subnet": "192.168.1.0/24", "peer": "192.168.1.9"},
]


@pytest.fixture
def env(tmp_path, monkeypatch):
    f = tmp_path / "aps.json"
    f.write_text(json.dumps({"wifi_aps": PROFILES}))
    monkeypatch.setattr(wifi_aps, "USER_FILE", tmp_path / "none.json")
    monkeypatch.setattr(wifi_aps, "LAB_FILE", tmp_path / "none2.json")
    monkeypatch.setattr(wifi_aps, "_legacy", lambda: None)
    monkeypatch.setattr(wifi_aps, "local_ipv4", lambda: ["192.168.50.128"])
    return {"EH_WIFI_APS_FILE": str(f)}


def test_explicit_ssid_wins(env):
    env.update(EH_TP_SSID="MINE", EH_TP_PASS="x", EH_WIFI="home")
    ap = wifi_aps.resolve(env, ips=["192.168.50.7"])
    assert (ap["ssid"], ap["password"], ap["_source"]) == ("MINE", "x", "EH_TP_SSID")


def test_explicit_ssid_takes_profile_password_and_peer(env):
    env.update(EH_TP_SSID="LAB_2G")
    ap = wifi_aps.resolve(env, ips=["192.168.50.7"])
    assert (ap["password"], ap["peer"]) == ("p1", "192.168.50.7")


@pytest.mark.parametrize("q,ssid", [("home", "HOME"), ("LAB_5g", "LAB_5G"), ("office bench", "LAB_2G"),
                                    ("office 2g", "LAB_2G")])
def test_query_matches_any_field(env, q, ssid):
    env["EH_WIFI"] = q
    assert wifi_aps.resolve(env, cp_target="esp32c5", ips=[])["ssid"] == ssid


def test_query_ambiguous_lists_masked(env):
    env["EH_WIFI"] = "office"
    with pytest.raises(wifi_aps.WifiApError) as e:
        wifi_aps.resolve(env, cp_target="esp32c5", ips=[])
    assert "LAB_2G" in str(e.value) and "LAB_5G" in str(e.value) and "p1" not in str(e.value)


def test_band_filter_narrows_query(env):
    env["EH_WIFI"] = "office"
    assert wifi_aps.resolve(env, cp_target="esp32s31", ips=[])["ssid"] == "LAB_2G"


def test_location_picks_2g_for_24ghz_cp(env):
    ap = wifi_aps.resolve(env, cp_target="esp32c6", ips=["10.0.0.2", "192.168.50.128"])
    assert (ap["ssid"], ap["peer"]) == ("LAB_2G", "192.168.50.128")


def test_location_ambiguous_for_5g_cp(env):
    with pytest.raises(wifi_aps.WifiApError):
        wifi_aps.resolve(env, cp_target="esp32c5", ips=["192.168.50.128"])


def test_location_uses_explicit_peer(env):
    ap = wifi_aps.resolve(env, ips=["192.168.1.20"])
    assert (ap["ssid"], ap["peer"]) == ("HOME", "192.168.1.9")


def test_nothing_matches_returns_none(env):
    assert wifi_aps.resolve(env, ips=["10.9.9.9"]) is None


def test_apply_env_keeps_caller_values(env):
    env["EH_TP_PEER"] = "1.2.3.4"
    wifi_aps.apply_env(env, cp_target="esp32c6")
    assert (env["EH_TP_SSID"], env["EH_TP_PEER"]) == ("LAB_2G", "1.2.3.4")



def test_apply_env_fills_empty_values(env):
    env.update(EH_TP_SSID="", EH_TP_PASS="", EH_TP_PEER="")
    wifi_aps.apply_env(env, cp_target="esp32c6")
    assert (env["EH_TP_SSID"], env["EH_TP_PASS"], env["EH_TP_PEER"]) == ("LAB_2G", "p1", "192.168.50.128")
