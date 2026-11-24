%:define D 1
D <: :> <% %> %: %:%: <%= <::>
a //* c */ b
x // y
#define S(x) #x
S(<:) S(a //* c */ b) S(%:%:)
#define F(x) %:x
#define C(a, b) a %:%: b
F(y) C(p, q)
#define X a // b
X
