package auth

type AuthCreatePayload struct {
	Name     string `json:"name" validate:"required,min=2,max=25"`
	Username string `json:"username" validate:"required,min=3,max=25"`
	Email    string `json:"email" validate:"required,email"`
	Password string `json:"password" validate:"required,min=8,max=255"`
}

type AuthLoginPayload struct {
	Email    string `json:"email" validate:"required,email,min=2,max=35"`
	Password string `json:"password" validate:"required,min=8,max=255"`
}

type ReqEmailConfirmPayload struct {
	Email string `json:"email" validate:"required,email,min=2,max=35"`
}
