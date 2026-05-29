## IroGB Test Suite

Our tests are driven by the python bindings for convenience, as it allows us to easily pull down test roms from the internet without having to rely on them as a dependency. Preparing the test suite can be done as follows:
```bash
pip install pytest        # We use pytest to drive all of our test cases
pip install pytest-xdist  # Optional, but recommended to run test cases in parallel
pip install -e .          # Compiles IroGB and exposes API via pip package
```

Then our tests can be run as you would run any other test suite driven by [pytest](https://docs.pytest.org/en/stable/). Keep in mind that most test cases will require an internet connection to pull the test roms down dynamically.

### Heuristics

The only test cases with external dependencies lie in `test_heuristic.py`, since these tests require a roms directory and backing ground-truth database to evaluate our MBC detection heuristic. The database is a spreadsheet that can be downloaded from [this link](https://gbhwdb.gekkio.fi/cartridges/). We **WILL NOT** provide a source location or means for distribution for any ROMs.

To include the heuristic test cases, two additional arguments must be passed:
```bash
pytest testing/          \
  --db <cartridges.csv>  \ # A full path to the database spreadsheet
  --roms <roms_dir>        # A full path to the roms directory
```

Again, tests can be run without these dependencies, but the heuristic test cases will be skipped.

## IroGB Test ROMs

These are a collection of test roms which will likely expand over time, which we use to observe specific hardware behavior. All tests we develop here will not be pushed unless they have been verified to pass on real hardware first, but regardless we suggest that anything we present in this directory is still used cautiously.

Compile any given rom using RGBASM, example:
```bash
rgbasm -o main.o main.asm
rgblink -o test.gb main.o
rgbfix -v -p 0 test.gb
```

