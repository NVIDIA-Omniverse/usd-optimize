-- Common Global Variables

newoption {
    trigger     = "usd-flavor",
    description = "(Optional) Specify the usd flavor",
    default = "usd"
}

newoption {
    trigger     = "usd-ver",
    description = "(Optional) Specify the usd version",
    default = "undefined"
}

newoption {
    trigger     = "python-ver",
    description = "(Optional) Specify the python version",
    allowed = {
        { "0", "No Python" },
        { "3.9", "Python 3.9" },
        { "3.10", "Python 3.10" },
        { "3.11", "Python 3.11" },
        { "3.12", "Python 3.12" }
    },
    default = "3.12"
}

USD_FLAVOR = _OPTIONS["usd-flavor"]
USD_VERSION = _OPTIONS["usd-ver"]
PYTHON_VERSION = _OPTIONS["python-ver"]
