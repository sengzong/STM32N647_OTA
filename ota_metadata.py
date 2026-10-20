"""Optional display metadata carried inside the existing UTF-8 description."""
PREFIX = "OTA-META1\n"


def _line(value, limit, field):
    value = str(value)
    if any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise ValueError(f"{field}不能包含换行或控制字符")
    if len(value.encode("utf-8")) > limit:
        raise ValueError(f"{field}最多 {limit} 个 UTF-8 字节")
    return value


def encode_metadata(name, version, model, description):
    name = _line(name, 127, "固件名称")
    version = _line(version, 63, "版本号")
    description = _line(description, 1000, "描述")
    if not name or model not in (0, 1, 2):
        raise ValueError("固件显示信息无效")
    return PREFIX + str(model) + "\n" + name + "\n" + version + "\n" + description


def decode_metadata(description):
    if not description.startswith(PREFIX):
        return None
    fields = description[len(PREFIX):].split("\n")
    if len(fields) != 4 or fields[0] not in ("0", "1", "2"):
        raise ValueError("固件显示信息格式错误")
    model, name, version, note = fields
    encode_metadata(name, version, int(model), note)
    return {"name": name, "version": version, "model": int(model), "description": note}
