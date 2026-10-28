"""`python -m fusedwasi` prints the directory of the C headers, to be used as `-I$(python -m fusedwasi)`."""

from . import get_include

print(get_include())
