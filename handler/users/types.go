package users

type UserUpdateProfilePayload struct {
	Username *string `json:"username" validate:"omitempty,min=3,max=25"`
	Name     *string `json:"name" validate:"omitempty,min=2,max=25"`
	Email    *string `json:"email" validate:"omitempty,email"`
	Password *string `json:"password" validate:"omitempty,min=8,max=255"`
}
