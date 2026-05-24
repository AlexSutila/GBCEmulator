#!/usr/bin/env python3
from dataclasses import dataclass
from pathlib import Path
import pytest
import csv


@dataclass
class RomCase:
    path: Path
    u1_kind: str
    u2_kind: str


def pytest_addoption(parser):
    parser.addoption("--db", action="store", help="For heuristic")
    parser.addoption("--roms", action="store", help="For heuristic")


def pytest_configure(config):
    config.addinivalue_line("markers", "heuristic: requires --db and --roms")


@pytest.fixture(scope="session")
def rom_paths(request):
    roms_path = request.config.getoption("roms")
    if not roms_path:
        pytest.exit("--roms is required", returncode=1)
    roms_path = Path(roms_path)
    return list(roms_path.glob("gb*/*.gb*"))


def pytest_generate_tests(metafunc):
    if "case" not in metafunc.fixturenames:
        return

    config = metafunc.config
    db_path = config.getoption("db")
    roms_path = config.getoption("roms")

    if not db_path or not roms_path:
        return

    db_path = Path(db_path)
    roms_path = Path(roms_path)

    db = {}
    with db_path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            db[row["game_name"]] = row

    # Build test cases
    cases = []
    for rom_file in roms_path.glob("gb*/*.gb*"):
        key = rom_file.stem

        info = db.get(key)
        if not info:
            continue

        cases.append(
            RomCase(path=rom_file, u1_kind=info["u1_kind"], u2_kind=info["u2_kind"])
        )

    metafunc.parametrize("case", cases)


def pytest_collection_modifyitems(config, items):
    db = config.getoption("db")
    roms = config.getoption("roms")

    if db and roms:
        return

    skip_marker = pytest.mark.skip(reason="need --db and --roms to run")

    for item in items:
        if "heuristic" in item.keywords:
            item.add_marker(skip_marker)


@pytest.fixture
def heuristic_env(request):
    db = request.config.getoption("db")
    roms = request.config.getoption("roms")
    return db, roms
