package users

type UserUpdateProfilePayload struct {
	Username        *string `json:"username" validate:"omitempty,min=3,max=25"`
	Name            *string `json:"name" validate:"omitempty,min=2,max=25"`
	Email           *string `json:"email" validate:"omitempty,email"`
	Phone           *string `json:"phone" validate:"omitempty,min=8,max=18"`
	Overview        *string `json:"overview" validate:"omitempty,min=1,max=125"`
	UsernameVisible *bool   `json:"username_visible" validate:"omitempty"`
	PhoneVisible    *bool   `json:"phone_visible" validate:"omitempty"`
	EmailVisible    *bool   `json:"email_visible" validate:"omitempty"`
	Password        *string `json:"password" validate:"omitempty,min=8,max=255"`
}
