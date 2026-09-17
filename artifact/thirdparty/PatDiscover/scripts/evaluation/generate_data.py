import json
import os
import pathlib
import random
import typing
import uuid
from enum import Enum


class AttributeType(Enum):
    BOOLEAN = 0
    ENUM = 1
    CONTINUOUS = 1
    DISTANCE = 2


class Attribute:

    def __init__(self, name: str, attribute_type: AttributeType):
        self.name = name
        self.attribute_type = attribute_type

    def generate(self, json_dict: dict[str, typing.Any]):
        raise NotImplementedError

class BooleanAttribute(Attribute):
    def __init__(self, name: str, attribute_type: AttributeType):
        super().__init__(name, attribute_type)

    def generate(self, json_dict: dict[str, typing.Any]):
        json_dict[self.name] = random.randint(0, 1)

class EnumAttribute(Attribute):
    def __init__(self, name: str, attribute_type: AttributeType, min_value: int, max_value: int):
        super().__init__(name, attribute_type)
        self.min_value = min_value
        self.max_value = max_value

    def generate(self, json_dict: dict[str, typing.Any]):
        json_dict[self.name] = random.randint(self.min_value, self.max_value)


class ContinuousAttribute(Attribute):
    def __init__(self, name: str, attribute_type: AttributeType, min_value: float, max_value: float, decimal_precision: int):
        super().__init__(name, attribute_type)
        self.min_value = min_value
        self.max_value = max_value
        self.decimal_precision = decimal_precision

    def generate(self, json_dict: dict[str, typing.Any]):
        json_dict[self.name] = round(random.uniform(self.min_value, self.max_value), self.decimal_precision)


class DistanceAttribute(Attribute):
    def __init__(self, name: str, attribute_type: AttributeType, x_min: float, x_max: float, y_min: float, y_max: float, z_min: float,
                 z_max: float, decimal_precision: int):
        super().__init__(name, attribute_type)
        self.x_min = x_min
        self.x_max = x_max
        self.y_min = y_min
        self.y_max = y_max
        self.z_min = z_min
        self.z_max = z_max

        self.decimal_precision = decimal_precision

    def generate(self, json_dict: dict[str, typing.Any]):
        json_dict[self.name] = {}
        json_dict[self.name]['x'] = round(random.uniform(self.x_min, self.x_max), self.decimal_precision)
        json_dict[self.name]['y'] = round(random.uniform(self.y_min, self.y_max), self.decimal_precision)
        json_dict[self.name]['z'] = round(random.uniform(self.z_min, self.z_max), self.decimal_precision)


def generate_item(attributes: list[Attribute]):
    data = {"id": uuid.uuid4().hex}
    for attribute in attributes:
        attribute.generate(data)

    return data


def main(attr_config_file: str, result_file: str, count: int):
    attributes = []

    with open(attr_config_file, "r") as f:
        attr_config = json.load(f)

        for item in attr_config:
            name = item["name"]

            if item["type"] == "Boolean":
                attributes.append(BooleanAttribute(name[:1].lower() + name[1:], AttributeType.BOOLEAN))
            if item["type"] == "EnumPrecise" or item["type"] == "EnumApprox":
                attributes.append(EnumAttribute(name[:1].lower() + name[1:], AttributeType.ENUM, item["minValue"], item["maxValue"]))
            elif item["type"] == "ContinuousPrecise" or item["type"] == "ContinuousApprox":
                attributes.append(
                    ContinuousAttribute(name[:1].lower() + name[1:], AttributeType.CONTINUOUS, item["minValue"], item["maxValue"],
                                        item["decimalPrecision"]))
            elif item["type"] == "DistancePrecise" or item["type"] == "DistanceApprox":
                attributes.append(
                    DistanceAttribute(name[:1].lower() + name[1:], AttributeType.DISTANCE, item["xMinValue"], item["xMaxValue"],
                                      item["yMinValue"], item["yMaxValue"], item["zMinValue"], item["zMaxValue"], item["decimalPrecision"]))

    result = []
    for i in range(count):
        result.append(generate_item(attributes))

    with open(result_file, "w") as f:
        json.dump(result, f)


if __name__ == "__main__":
    p = pathlib.Path(__file__)
    os.chdir(p.parent.parent.parent)
    config_file = "data/attribute_config/default_approx.json"

    main(config_file, "data.json", 4096)
