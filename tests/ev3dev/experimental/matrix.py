from pybricks.experimental import Matrix, Vector, UnitVector

# Basic matrix algebra
A = Matrix(
    [
        [1, 2, 3],
        [4, 5, 6],
        [7, 8, 9],
    ]
)
B = -A
C = A - A.T

print("A =", A)
print("B = -A =", B)
print("A.T =", A.T)
print("A + B =", A + B)
print("C = A - A.T =", C)
print("C + C.T =", C + C.T)
print("A * A.T =", A * A.T)
print("len(A) =", len(A))
print("A * 3 =", A * 3)
print("3 * A =", 3 * A)

# Vector basics
b = Vector(3, 4, 0)
c = b.T
print("b = Vector(3, 4, 0) =", b)
print("len(b) =", len(b))
print("abs(b) =", abs(b))
print("c = b.T", c)
print("A * b =", A * b)
u = UnitVector(3, 4, 0)
print("u = UnitVector(3, 4, 0) =", u)
print("abs(u) =", abs(u))

# Dealing with resulting scalar types
print("b.T * b =", b.T * b)
print("type(b.T * b) =", type(b.T * b))
print("b.T * A * b =", b.T * A * b)
print("(b.T * A * b) * A / 2 =", (b.T * A * b) * A / 2)

# Nonsquare matrices
D = Matrix(
    [
        [0, 1, 0, 2],
        [3, 0, 4, 0],
    ]
)
print("D =", D)
print("len(D) =", len(D))
print("len(D.T) =", len(D.T))
print("D.T * D =", D.T * D)
print("D * D.T =", D * D.T)

# Test catch of dimension errors
try:
    b - c
except ValueError:
    pass
try:
    b * A
except ValueError:
    pass

# B points to A, test that data stays alive
del A
print("B = -A =", B)
