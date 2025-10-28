package httperr

import "errors"

var (
	Err_DuplicateEmail          = errors.New("пользователь с таким адресом электронной почты уже существует")
	Err_UserNotFound            = errors.New("пользователь не был найден")
	Err_ContextDeadlineExceeded = errors.New("время ожидания ответа базы данных истекло, пожалуйста, повторите попытку позже")
	Err_ContextCanceled         = errors.New("операция была отменена")
	Err_UniqueViolation         = errors.New("введенные данные уже существуют")
	Err_DbTimeout               = errors.New("время ожидания подключения к базе данных истекло")
	Err_DbNetworkTemporary      = errors.New("временная проблема с сетью. пожалуйста, попробуйте снова")
	Err_DbNetwork               = errors.New("сетевая ошибка при подключении к базе данных")
	Err_NotDeleted              = errors.New("ошибка: ни одна запись не была удалена")
	Err_NotUpdated              = errors.New("ошибка: запись не была обновлена")
)
