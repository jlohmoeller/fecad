class Variable:
    variables = dict()

    def __init__(self, name: str, value: float, description="", **kwargs):
        self.name = name
        self.value = value
        self.description = description
        self.unit = ""
        self.precision = None

        if "unit" in kwargs:
            self.unit = kwargs["unit"]
        if "precision" in kwargs:
            self.precision = kwargs["precision"]

    def render(self) -> str:
        v = self.value
        u = self.unit
        if isinstance(v,str):
            return f"\\DefineVariable{{{self.name}}}{{\\mbox{{{v}}}}}{{{self.description}}}"
        if "percent" in self.name or "-pct-" in self.name:
            v *= 100
            if u == "":
                u = "percent"
        if self.precision is None:
            if "count" in self.name or "-cnt-" in self.name:
                self.precision = 0
            else:
                if v > 10:
                    self.precision=0
                elif v > 1:
                    self.precision=1
                else:
                    self.precision=2
        if u != "":
            unit_val = f"\\qty{{{v:.{self.precision}f}}}{{\\{u}}}"
            return f"\\DefineVariable{{{self.name}}}{{{unit_val}}}{{{self.description}}}"
        elif v < 13 and self.precision == 0:
            v = round(v, self.precision)
        return f"\\DefineVariable{{{self.name}}}{{{v:,.{self.precision}f}}}{{{self.description}}}"

def store_variables(filename):
    with open(filename, mode="w+") as f:
        for _, v in sorted(Variable.variables.items()):
            f.write(v.render() + "\n")

def register_variable(name, value, description="", **kwargs):
    if name in Variable.variables and Variable.variables[name].value != value:
        print(f"redefining variable {name}")
    v = Variable(name, value, description, **kwargs)
    Variable.variables[name] = v
    if kwargs.get("print", True):
        print(v.render())
