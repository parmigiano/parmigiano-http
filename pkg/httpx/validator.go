package httpx

import (
	"fmt"
	"regexp"

	"github.com/go-playground/validator"
)

var Validate *validator.Validate
var passwordRegex = regexp.MustCompile(`^[A-Za-z0-9!"#$%&'()*+\-./:;<=>?@[\\\]^_{|}~]{0,8}$`)

func init() {
	Validate = validator.New()

	_ = Validate.RegisterValidation("optional_uuid", func(fl validator.FieldLevel) bool {
		uuid := fl.Field().String()
		if uuid == "" {
			return true
		}

		return validator.New().Var(uuid, "uuid") == nil
	})

	_ = Validate.RegisterValidation("dpass", func(fl validator.FieldLevel) bool {
		pass := fl.Field().String()
		if pass == "" {
			return true
		}

		return passwordRegex.MatchString(pass)
	})
}

func ValidateMsg(err error) string {
	if validationErrors, ok := err.(validator.ValidationErrors); ok {
		for _, fieldError := range validationErrors {
			switch fieldError.Tag() {
			case "required":
				return fmt.Sprintf("поле %s является обязательным для заполнения", fieldError.Field())
			case "len":
				return fmt.Sprintf("длина поля %s должна составлять %s символов", fieldError.Field(), fieldError.Param())
			case "numeric":
				return fmt.Sprintf("поле %s должно содержать только цифры", fieldError.Field())
			case "uuid":
				return fmt.Sprintf("поле %s должно быть действительным UUID", fieldError.Field())
			case "gt":
				return fmt.Sprintf("поле %s должно быть больше, чем %s", fieldError.Field(), fieldError.Param())
			case "max":
				return fmt.Sprintf("количество символов в поле %s не может превышать %s символов", fieldError.Field(), fieldError.Param())
			case "min":
				return fmt.Sprintf("поле %s должно содержать не менее %s символов", fieldError.Field(), fieldError.Param())
			case "optional_uuid":
				return fmt.Sprintf("поле %s должно быть допустимым UUID или пустым", fieldError.Field())
			default:
				return fmt.Sprintf("ошибка проверки в поле %s", fieldError.Field())
			}
		}
	}

	return "ошибка проверки запроса"
}
