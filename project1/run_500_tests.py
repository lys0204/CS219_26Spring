#!/usr/bin/env python3
import random
import subprocess
from decimal import Decimal, ROUND_DOWN, getcontext
from pathlib import Path

getcontext().prec = 120

SEED = 20260320
TARGET_SCALE = 18
TOTAL_CASES = 500

INVALID_CASES = 80
LOW_PREC_CASES = 170
HIGH_PREC_CASES = 170
SCI_MIX_CASES = 80

BINARY_OPS = ["+", "-", "*", "/", "^"]
UNARY_OPS = ["sqrt", "square"]


def format_operand(v: Decimal) -> str:
    s = format(v, "f")
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    if s == "-0":
        s = "0"
    return s


def normalize_decimal_str(v: Decimal) -> str:
    s = format(v, "f")
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    if s == "-0":
        s = "0"
    return s


def decimal_to_scale18_trunc(v: Decimal) -> Decimal:
    return v.quantize(Decimal("1e-18"), rounding=ROUND_DOWN)


def random_decimal(rng: random.Random, max_int: int, max_frac_digits: int, sign_prob: float) -> Decimal:
    sign = -1 if rng.random() < sign_prob else 1
    int_part = rng.randint(0, max_int)
    frac_digits = rng.randint(0, max_frac_digits)
    frac_part = rng.randint(0, (10 ** frac_digits) - 1) if frac_digits > 0 else 0
    if frac_digits > 0:
        s = f"{int_part}.{frac_part:0{frac_digits}d}"
    else:
        s = str(int_part)
    return Decimal(sign) * Decimal(s)


def random_low_precision_decimal(rng: random.Random) -> Decimal:
    return random_decimal(rng, max_int=5000, max_frac_digits=2, sign_prob=0.35)


def random_high_precision_decimal(rng: random.Random) -> Decimal:
    return random_decimal(rng, max_int=500000, max_frac_digits=12, sign_prob=0.35)


def to_scientific_str(v: Decimal, exp: int, use_upper_e: bool) -> str:
    sign = "-" if v < 0 else ""
    av = abs(v)
    mantissa = format_operand(av)
    e_char = "E" if use_upper_e else "e"
    exp_sign = "+" if exp >= 0 else ""
    return f"{sign}{mantissa}{e_char}{exp_sign}{exp}"


def make_binary_case(rng: random.Random, high_precision: bool):
    op = rng.choice(BINARY_OPS)
    rnd = random_high_precision_decimal if high_precision else random_low_precision_decimal

    if op == "^":
        base_dec = rnd(rng)
        exp_max = 6 if high_precision else 10
        exp_dec = Decimal(rng.randint(0, exp_max))
        a = format_operand(base_dec)
        b = format_operand(exp_dec)
    else:
        a_dec = rnd(rng)
        b_dec = rnd(rng)
        if op == "/":
            while b_dec == 0:
                b_dec = rnd(rng)
        a = format_operand(a_dec)
        b = format_operand(b_dec)

    expr = f"{a} {op} {b}"
    return expr, {"kind": "binary", "a": a, "op": op, "b": b}


def make_unary_case(rng: random.Random, high_precision: bool):
    op = rng.choice(UNARY_OPS)
    rnd = random_high_precision_decimal if high_precision else random_low_precision_decimal

    arg_dec = rnd(rng)
    if op == "sqrt":
        while arg_dec < 0:
            arg_dec = rnd(rng)

    arg = format_operand(arg_dec)
    expr = f"{op}({arg})"
    return expr, {"kind": "unary", "op": op, "arg": arg}


def make_scientific_mixed_case(rng: random.Random):
    if rng.random() < 0.80:
        op = rng.choice(BINARY_OPS)
        sci_on_left = (rng.random() < 0.5)

        normal_dec = random_decimal(rng, max_int=50000, max_frac_digits=6, sign_prob=0.35)
        sci_base = random_decimal(rng, max_int=9999, max_frac_digits=6, sign_prob=0.35)
        while sci_base == 0:
            sci_base = random_decimal(rng, max_int=9999, max_frac_digits=6, sign_prob=0.35)
        sci_exp = rng.randint(-8, 8)
        sci_str = to_scientific_str(sci_base, sci_exp, use_upper_e=(rng.random() < 0.5))

        if op == "^":
            base_str = sci_str if sci_on_left else format_operand(normal_dec)
            exp_val = rng.randint(0, 6)
            expr = f"{base_str} ^ {exp_val}"
            return expr, {"kind": "binary", "a": base_str, "op": "^", "b": str(exp_val)}

        if op == "/":
            while normal_dec == 0:
                normal_dec = random_decimal(rng, max_int=50000, max_frac_digits=6, sign_prob=0.35)

        normal_str = format_operand(normal_dec)
        if sci_on_left:
            a, b = sci_str, normal_str
        else:
            a, b = normal_str, sci_str

        if op == "/" and Decimal(b) == 0:
            b = "1"

        expr = f"{a} {op} {b}"
        return expr, {"kind": "binary", "a": a, "op": op, "b": b}

    op = rng.choice(UNARY_OPS)
    sci_base = random_decimal(rng, max_int=9999, max_frac_digits=6, sign_prob=0.35)
    while sci_base == 0:
        sci_base = random_decimal(rng, max_int=9999, max_frac_digits=6, sign_prob=0.35)
    sci_exp = rng.randint(-8, 8)
    arg = to_scientific_str(sci_base, sci_exp, use_upper_e=(rng.random() < 0.5))

    if op == "sqrt" and Decimal(arg) < 0:
        arg = to_scientific_str(abs(sci_base), sci_exp, use_upper_e=(rng.random() < 0.5))

    expr = f"{op}({arg})"
    return expr, {"kind": "unary", "op": op, "arg": arg}


def make_invalid_cases() -> list[str]:
    return [
        "",
        "   ",
        "1",
        "+",
        "1 +",
        "+ 1",
        "1 ++ 2",
        "1 -- 2",
        "1 ** 2",
        "1 // 2",
        "1 ? 2",
        "abc + 1",
        "1 + abc",
        "sqrt()",
        "square()",
        "sqrt(-1)",
        "sqrt(1 + 2)",
        "square(1 + 2)",
        "sqrt(1",
        "square(1",
        "sqrt1)",
        "square1)",
        "1e + 2",
        "1e- + 2",
        "1e +",
        "1 2",
        "1 + 2 + 3",
        "1 + +2 +3",
        "quit",
        "exit",
    ]


def build_test_plan(rng: random.Random):
    cases = []

    if INVALID_CASES + LOW_PREC_CASES + HIGH_PREC_CASES + SCI_MIX_CASES != TOTAL_CASES:
        raise ValueError("Case counts must sum to TOTAL_CASES.")

    invalid_pool = make_invalid_cases()
    for i in range(INVALID_CASES):
        expr = invalid_pool[i % len(invalid_pool)]
        cases.append(("invalid", expr, None))

    for _ in range(LOW_PREC_CASES):
        if rng.random() < 0.75:
            expr, meta = make_binary_case(rng, high_precision=False)
        else:
            expr, meta = make_unary_case(rng, high_precision=False)
        cases.append(("low", expr, meta))

    for _ in range(HIGH_PREC_CASES):
        if rng.random() < 0.75:
            expr, meta = make_binary_case(rng, high_precision=True)
        else:
            expr, meta = make_unary_case(rng, high_precision=True)
        cases.append(("high", expr, meta))

    for _ in range(SCI_MIX_CASES):
        expr, meta = make_scientific_mixed_case(rng)
        cases.append(("sci", expr, meta))

    rng.shuffle(cases)
    return cases


def expected_binary_result(a: str, op: str, b: str) -> str:
    da = Decimal(a)
    db = Decimal(b)

    if op == "+":
        out = da + db
    elif op == "-":
        out = da - db
    elif op == "*":
        out = da * db
    elif op == "/":
        out = decimal_to_scale18_trunc(da / db)
    elif op == "^":
        out = da ** int(db)
    else:
        raise ValueError(f"Unsupported op: {op}")

    return normalize_decimal_str(out)


def expected_unary_result(op: str, arg: str) -> str:
    darg = Decimal(arg)

    if op == "square":
        out = darg * darg
    elif op == "sqrt":
        out = decimal_to_scale18_trunc(darg.sqrt())
    else:
        raise ValueError(f"Unsupported unary op: {op}")

    return normalize_decimal_str(out)


def expected_result(meta: dict) -> str:
    if meta["kind"] == "binary":
        return expected_binary_result(meta["a"], meta["op"], meta["b"])
    if meta["kind"] == "unary":
        return expected_unary_result(meta["op"], meta["arg"])
    raise ValueError(f"Unknown case kind: {meta['kind']}")


def run_calculator(exe: Path, expr: str):
    p = subprocess.run(
        [str(exe), expr],
        text=True,
        capture_output=True,
        timeout=5,
    )
    stdout = p.stdout.strip()
    stderr = p.stderr.strip()

    if p.returncode != 0:
        return False, f"NONZERO({p.returncode}): {stdout} {stderr}".strip()

    # Binary output form: "a op b = result"
    if "=" in stdout:
        got = stdout.split("=", 1)[1].strip()
        return True, got

    return True, stdout


def main():
    root = Path(__file__).resolve().parent
    exe = root / "calculator"
    if not exe.exists():
        print("ERROR: ./calculator not found. Compile first:")
        print("  gcc -std=c11 -Wall -Wextra -pedantic calculator.c -o calculator")
        return 1

    rng = random.Random(SEED)
    cases = build_test_plan(rng)

    passed = 0
    failed = 0
    failures = []
    stats = {
        "invalid": {"total": 0, "passed": 0, "failed": 0},
        "low": {"total": 0, "passed": 0, "failed": 0},
        "high": {"total": 0, "passed": 0, "failed": 0},
        "sci": {"total": 0, "passed": 0, "failed": 0},
    }

    for i, (category, expr, meta) in enumerate(cases, start=1):
        stats[category]["total"] += 1
        ok, got = run_calculator(exe, expr)

        if category == "invalid":
            if ok:
                failed += 1
                stats[category]["failed"] += 1
                failures.append((i, category, expr, "NONZERO(error)", got))
            else:
                passed += 1
                stats[category]["passed"] += 1
            continue

        expected = expected_result(meta)

        if not ok:
            failed += 1
            stats[category]["failed"] += 1
            failures.append((i, category, expr, expected, got))
            continue

        if got == expected:
            passed += 1
            stats[category]["passed"] += 1
        else:
            failed += 1
            stats[category]["failed"] += 1
            failures.append((i, category, expr, expected, got))

    print(f"Seed: {SEED}")
    print(f"Total: {len(cases)}")
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")
    print("Breakdown:")
    print(f"  invalid: total={stats['invalid']['total']} passed={stats['invalid']['passed']} failed={stats['invalid']['failed']}")
    print(f"  low:     total={stats['low']['total']} passed={stats['low']['passed']} failed={stats['low']['failed']}")
    print(f"  high:    total={stats['high']['total']} passed={stats['high']['passed']} failed={stats['high']['failed']}")
    print(f"  sci:     total={stats['sci']['total']} passed={stats['sci']['passed']} failed={stats['sci']['failed']}")

    if failures:
        report = root / "test_failures.txt"
        with report.open("w", encoding="utf-8") as f:
            for idx, category, expr, exp, got in failures:
                f.write(f"[{idx}] ({category}) {expr}\n")
                f.write(f"  expected: {exp}\n")
                f.write(f"  got:      {got}\n\n")
        print(f"Failure details written to: {report}")
        return 2

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
