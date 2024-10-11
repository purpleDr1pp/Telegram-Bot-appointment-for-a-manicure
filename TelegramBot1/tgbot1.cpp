#include <stdio.h>
#include <tgbot/tgbot.h>
#define SQLITECPP_COMPILE_DLL
#include <SQLiteCpp/SQLiteCpp.h>
#include <regex>
#include <iostream>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <thread>
#include <chrono>

// Структура для хранения данных сессии пользователя
struct UserData {
    std::string phoneNumber;
    std::string name;
    bool registration = false;
    std::list<std::string> availableDays;
    std::string day;
    std::string type;
    std::list<std::string> time;
    std::string selectTime;
    int monthCnt;
    std::string month;
};

// Карта, где ключом является идентификатор пользователя (chat_id), а значением данные сессии пользователя
std::map<long, UserData> sessions;

bool registration(const SQLite::Database& db, TgBot::Message::Ptr message) {
    try {
        SQLite::Statement registr(db, "SELECT EXISTS(SELECT 1 FROM users WHERE id = ?)");
        registr.bind(1, message->chat->id);
        if (registr.executeStep()) {
            return registr.getColumn(0).getInt() == 1;
        }
        return false;
    }
    catch (std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        return false;
    }
}

bool isValidPhoneNumber(const std::string& phoneNumber) {
    // Регулярное выражение для проверки формата номера телефона
    std::regex phoneRegex("\\+?[0-9]{11,12}");

    // Проверка, соответствует ли введенная строка формату номера телефона
    return std::regex_match(phoneNumber, phoneRegex);
}

std::list<std::string> availableTime(const SQLite::Database& db, UserData& userData, std::string& month) {
    std::list<std::string> time;
    SQLite::Statement timequery(db, "SELECT time FROM " + month + " WHERE day = ?");
    timequery.bind(1, userData.day);

    while (timequery.executeStep()) {
        time.push_back(timequery.getColumn(0).getText());
    }
    return time;
}
bool available(const SQLite::Database& db, const std::string& selectDay, const std::string& selectTime, std::string month) {
    try {
        SQLite::Statement availableDB(db, "SELECT EXISTS(SELECT 1 FROM " + month + " WHERE day = ? AND time = ? AND available = 1)");
        availableDB.bind(1, selectDay);
        availableDB.bind(2, selectTime);
        if (availableDB.executeStep()) {
            return availableDB.getColumn(0).getInt() == 1;
        }
        return false;
    }
    catch (std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        return false;
    }
}

std::list<std::string> allAvailableDays(const SQLite::Database& db, const int& monthCnt, const std::vector<std::string>& months) {
    std::list<std::string> availableDays;
    std::string tableName = months[monthCnt];
    std::string query = "SELECT day, available FROM " + tableName;
    try {
        SQLite::Statement days(db, query);

        while (days.executeStep()) {
            if (days.getColumn(1).getInt() == 0) {
                availableDays.push_back("empty");
            }
            else {
                if (std::find(availableDays.begin(), availableDays.end(), days.getColumn(0).getText()) == availableDays.end()) {
                    availableDays.push_back(days.getColumn(0).getText());
                }
            }
        }
    }
    catch (std::exception& e) {
        std::cerr << "Error during SQL query execution: " << e.what() << std::endl;
    }
    return availableDays;
}

/*Функция для отправки сообщения с таблицей с кнопками*/
void table(const SQLite::Database& db, const TgBot::Bot& bot, const TgBot::Message::Ptr& message, int& monthCnt, const std::vector<std::string>& months) {
    try {
        TgBot::InlineKeyboardMarkup::Ptr keyboard(new TgBot::InlineKeyboardMarkup);
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;

        if (monthCnt > 11) {
            monthCnt = 0;
        }
        else if (monthCnt < 0) {
            monthCnt = 11;
        }

        TgBot::InlineKeyboardButton::Ptr prevMonthBtn(new TgBot::InlineKeyboardButton);
        prevMonthBtn->text = "<";
        prevMonthBtn->callbackData = "prev_month";
        row.push_back(prevMonthBtn);

        // Добавляем кнопку с названием месяца
        TgBot::InlineKeyboardButton::Ptr monthBtn(new TgBot::InlineKeyboardButton);
        monthBtn->text = months[monthCnt];
        monthBtn->callbackData = "current_month";
        row.push_back(monthBtn);

        // Добавляем кнопку для переключения на следующий месяц
        TgBot::InlineKeyboardButton::Ptr nextMonthBtn(new TgBot::InlineKeyboardButton);
        nextMonthBtn->text = ">";
        nextMonthBtn->callbackData = "next_month";
        row.push_back(nextMonthBtn);

        keyboard->inlineKeyboard.push_back(row);
        row.clear();

        std::list<std::string> week = { u8"Пн", u8"Вт", u8"Ср", u8"Чт", u8"Пт" };
        for (const auto& day : week) {
            TgBot::InlineKeyboardButton::Ptr weekBtn(new TgBot::InlineKeyboardButton);
            weekBtn->text = day;
            weekBtn->callbackData = "weekBtn";
            row.push_back(weekBtn);
        }
        keyboard->inlineKeyboard.push_back(row);
        row.clear();

        int cnt = 0;
        std::list<std::string> availabledays = allAvailableDays(db, monthCnt, months);
        for (const auto& day : availabledays) {
            if (cnt <= 4) {
                TgBot::InlineKeyboardButton::Ptr button(new TgBot::InlineKeyboardButton);
                if (day == "empty") {
                    button->text = " ";
                    button->callbackData = "empty";
                    row.push_back({ button });
                    cnt += 1;
                }
                else {
                    button->text = day;
                    button->callbackData = day;
                    row.push_back({ button });
                    cnt += 1;
                }
            }
            else {
                cnt = 1;
                keyboard->inlineKeyboard.push_back(row);
                row.clear();
                TgBot::InlineKeyboardButton::Ptr button(new TgBot::InlineKeyboardButton);
                if (day == "empty") {
                    button->text = " ";
                    button->callbackData = "empty";
                    row.push_back({ button });
                }
                else {
                    button->text = day;
                    button->callbackData = day;
                    row.push_back({ button });
                }
            }
        }

        if (!row.empty()) {
            keyboard->inlineKeyboard.push_back(row);
        }

        row.clear();
        TgBot::InlineKeyboardButton::Ptr lowBtn(new TgBot::InlineKeyboardButton);
        lowBtn->text = u8"Выберите дату";
        lowBtn->callbackData = "lowBtn";
        row.push_back(lowBtn);
        keyboard->inlineKeyboard.push_back(row);
        row.clear();

        bot.getApi().sendMessage(message->chat->id, "Here are the available days:", false, 0, keyboard);
    }
    catch (std::exception& e) {
        std::cerr << "Error during SQL query execution: " << e.what() << std::endl;
    }
}

boost::posix_time::ptime parseStringToPtime(const std::string& datetimeStr) {
    std::stringstream ss(datetimeStr);
    boost::posix_time::ptime time;

    // Получение текущей даты
    boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
    int currentYear = now.date().year();

    // Формат даты и времени с учетом текущего года
    std::string timeWithYear = std::to_string(currentYear) + "." + datetimeStr; // Добавляем текущий год к строке времени
    ss.str(timeWithYear);

    boost::posix_time::time_input_facet* tif = new boost::posix_time::time_input_facet;
    tif->format("%Y.%d.%m %H:%M"); // Используем формат с годом
    ss.imbue(std::locale(ss.getloc(), tif));

    ss >> time;

    return time;
}

void notificationThread(SQLite::Database& db, TgBot::Bot& bot) {
    const int HOUR_INTERVAL = 15;

    while (true) {
        SQLite::Statement notifications(db, "SELECT notification, id FROM users");

        while (notifications.executeStep()) {
            std::string mytime = notifications.getColumn(0).getText();
            int id = std::stoi(notifications.getColumn(1).getText());
            std::string time = mytime;

            boost::posix_time::ptime currentTime = boost::posix_time::second_clock::local_time();
            boost::posix_time::ptime timeFromDB = parseStringToPtime(time);
            boost::posix_time::time_duration diff = timeFromDB - currentTime;

            if (diff.total_seconds() <= 24 * 3600 && !time.empty()) {
                bot.getApi().sendMessage(id, u8"Напоминание!! Вы записаны на " + mytime);
                SQLite::Statement updateQuery(db, "UPDATE users SET notification = NULL, available = 1, day_and_time = NULL, month = NULL, type = NULL WHERE day_and_time = ?");
                updateQuery.bind(1, mytime);
                updateQuery.exec();

                std::cout << "Record deleted for time: " << mytime << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(HOUR_INTERVAL));
    }
}

int main() {
    SQLite::Database db("DB.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);


    std::list<std::string> types = { u8"Ногти", u8"Ресницы", u8"Стопа" };
    std::vector<std::string> months = { u8"Январь", u8"Февраль", u8"Март", u8"Апрель", u8"Май", u8"Июнь", u8"Июль", u8"Август", u8"Сентябрь", u8"Октябрь", u8"Ноябрь", u8"Декабрь" };

    TgBot::Bot bot("YOUR BOT TOKEN");

    std::thread notificationWorker(notificationThread, std::ref(db), std::ref(bot));
    notificationWorker.detach();

    while (true) {
        // команда start
        bot.getEvents().onCommand("start", [&bot, &db](TgBot::Message::Ptr message) {
            try {
                long chatId = message->chat->id;
                if (sessions.find(chatId) == sessions.end()) {
                    // Если сессии нет, создаем новую
                    sessions[chatId] = UserData();
                }
                UserData& userData = sessions[chatId];
                bot.getApi().sendMessage(chatId, u8"прив");
                if (!registration(db, message)) {
                    bot.getApi().sendMessage(chatId, u8"Вы не зарегистрированы. Введите номер телефона");
                    userData.registration = true;
                }
            }
            catch (std::exception& e) {
                std::cerr << "Exception occurred: " << e.what() << std::endl;
            }
            });

        bot.getEvents().onCommand("time", [&bot, &db, &months](TgBot::Message::Ptr message) {
            long chatId = message->chat->id;
            if (sessions.find(chatId) == sessions.end()) {
                // Если сессии нет, создаем новую
                sessions[chatId] = UserData();
            }
            UserData& userData = sessions[chatId];
            if (registration(db, message) == true) {
                SQLite::Statement users(db, "SELECT COUNT(*) FROM users WHERE id = ? and available = 0");
                users.bind(1, message->chat->id);
                if (users.executeStep()) {
                    int count = users.getColumn(0).getInt();
                    if (count > 0) {
                        bot.getApi().sendMessage(message->chat->id, u8"Извините, но вы уже записаны");
                    }
                    else {
                        boost::posix_time::ptime currentTime = boost::posix_time::second_clock::local_time();
                        int currentMonth = currentTime.date().month();
                        userData.monthCnt = currentMonth - 1;
                        table(db, bot, message, userData.monthCnt, months);
                    }
                }
            }
            else {
                bot.getApi().sendMessage(message->chat->id, u8"Чтобы пользоваться ботом, необходимо пройти регистрацию (Введите /start)");
            }
            });
        bot.getEvents().onCallbackQuery([&bot, &db, &types, &months](TgBot::CallbackQuery::Ptr query) {
            try {
                if (registration(db, query->message) == true) {
                    long chatId = query->message->chat->id;
                    if (sessions.find(chatId) == sessions.end()) {
                        bot.getApi().sendMessage(query->message->chat->id, u8"Что-то пошло не так. Попробуйте еще раз (/time)", false, 0);
                        bot.getApi().deleteMessage(query->message->chat->id, query->message->messageId);
                    }
                    else {
                        UserData& userData = sessions[chatId];
                        if (query->data == "prev_month") {
                            userData.monthCnt -= 1;
                            bot.getApi().deleteMessage(query->message->chat->id, query->message->messageId);
                            table(db, bot, query->message, userData.monthCnt, months);
                        }
                        else if (query->data == "next_month") {
                            userData.monthCnt += 1;
                            bot.getApi().deleteMessage(query->message->chat->id, query->message->messageId);
                            table(db, bot, query->message, userData.monthCnt, months);
                        }
                        else if (query->data == "current_month" || query->data == "empty" || query->data == "weekBtn" || query->data == "lowBtn") {

                        }
                        else {
                            if (userData.day.empty() && userData.type.empty()) {
                                userData.day = query->data;
                                userData.month = months[userData.monthCnt];
                                bot.getApi().deleteMessage(query->message->chat->id, query->message->messageId);

                                TgBot::InlineKeyboardMarkup::Ptr menu(new TgBot::InlineKeyboardMarkup);

                                for (const auto& i : types) {
                                    TgBot::InlineKeyboardButton::Ptr button(new TgBot::InlineKeyboardButton);
                                    button->text = i;
                                    button->callbackData = i;
                                    menu->inlineKeyboard.push_back({ button });
                                }

                                bot.getApi().sendMessage(query->message->chat->id, u8"Выберите тип услуги:", false, 0, menu);

                            }
                            else if (userData.type.empty()) {
                                userData.type = query->data;
                                bot.getApi().deleteMessage(query->message->chat->id, query->message->messageId);

                                TgBot::InlineKeyboardMarkup::Ptr menu(new TgBot::InlineKeyboardMarkup);

                                userData.time = availableTime(db, userData, userData.month);
                                for (const auto& time : userData.time) {
                                    TgBot::InlineKeyboardButton::Ptr button(new TgBot::InlineKeyboardButton);
                                    button->text = time;
                                    button->callbackData = time;
                                    menu->inlineKeyboard.push_back({ button });
                                }
                                bot.getApi().sendMessage(query->message->chat->id, u8"Выберите время: (день недели: " + userData.day + ")", false, 0, menu);

                            }
                            else {
                                userData.selectTime = query->data;
                                bot.getApi().deleteMessage(query->message->chat->id, query->message->messageId);
                                if (!available(db, userData.day, userData.selectTime, userData.month)) {
                                    bot.getApi().sendMessage(query->message->chat->id, u8"Что-то пошло не так. Возможно вы пытаетесь записаться на время, которое уже не доступно.");
                                }
                                else {
                                    db.exec("BEGIN TRANSACTION");
                                    SQLite::Statement updateQuery(db, "UPDATE " + userData.month + " SET available = 0, id = ?, type = ? WHERE day = ? AND time = ?");
                                    updateQuery.bind(1, query->message->chat->id);
                                    updateQuery.bind(2, userData.type);
                                    updateQuery.bind(3, userData.day);
                                    updateQuery.bind(4, userData.selectTime);
                                    updateQuery.exec();

                                    //bot.getApi().sendMessage(382500548, u8"У вас новая запись\n" + userData.day + ", " + userData.selectTime + ", " + userData.type);
                                    SQLite::Statement updateUsers(db, "UPDATE users SET available = 0, day_and_time = ?, type = ?, notification = ?, month = ? WHERE id = ?");
                                    updateUsers.bind(1, userData.day + ' ' + userData.selectTime);
                                    updateUsers.bind(2, userData.type);
                                    updateUsers.bind(3, userData.day + ' ' + userData.selectTime);
                                    updateUsers.bind(4, userData.month);
                                    updateUsers.bind(5, query->message->chat->id);
                                    updateUsers.exec();
                                    db.exec("COMMIT");

                                    std::string message = u8"Ура ты кру поздр ващеее. ты записан на: " + userData.type + u8", в " + userData.selectTime + u8", " + userData.day;
                                    bot.getApi().sendMessage(query->message->chat->id, message);
                                    userData.type.clear();
                                    userData.selectTime.clear();
                                    userData.day.clear();
                                    userData.month.clear();
                                    userData.monthCnt = 0;
                                }
                            }
                        }
                    }
                }
                else {
                    bot.getApi().sendMessage(query->message->chat->id, u8"Чтобы пользоваться ботом, необходимо пройти регистрацию (Введите /start)");
                }
            }
            catch (std::exception& e) {
                std::cerr << "Exception occurred: " << e.what() << std::endl;
                db.exec("ROLLBACK");
            }
            });


        bot.getEvents().onCommand("cancel", [&bot, &db](TgBot::Message::Ptr message) {
            try {
                long chatId = message->chat->id;
                if (sessions.find(chatId) == sessions.end()) {
                    // Если сессии нет, создаем новую
                    sessions[chatId] = UserData();
                }
                UserData& userData = sessions[chatId];

                db.exec("BEGIN TRANSACTION");

                if (registration(db, message) == true) {
                    SQLite::Statement query(db, "SELECT COUNT(*) FROM users WHERE id = ? and available = 0");
                    query.bind(1, message->chat->id);
                    if (query.executeStep()) {
                        int count = query.getColumn(0).getInt();
                        if (count > 0) {
                            SQLite::Statement montQuery(db, "SELECT month FROM users WHERE id = ?");
                            montQuery.bind(1, message->chat->id);
                            montQuery.executeStep();
                            userData.month = montQuery.getColumn(0).getText();
                            std::cout << userData.month;
                            std::string query = "UPDATE " + userData.month + " SET available = 1, id = NULL, type = NULL WHERE id = ?";
                            SQLite::Statement updateQuery(db, query);
                            updateQuery.bind(1, message->chat->id);
                            updateQuery.exec();

                            SQLite::Statement updateUsers(db, "UPDATE users SET available = 1, day_and_time = NULL, type = NULL, notification = NULL, month = NULL WHERE id = ?");
                            updateUsers.bind(1, message->chat->id);
                            updateUsers.exec();
                            bot.getApi().sendMessage(message->chat->id, u8"Ваша запись успешно отменена.");
                            // bot.getApi().sendMessage(382500548, u8"Клиент только что отменил запись");
                        }
                        else {
                            bot.getApi().sendMessage(message->chat->id, u8"У вас нет ни одной записи. Чтобы записаться используйте команду /time");
                        }
                    }
                }
                else {
                    bot.getApi().sendMessage(message->chat->id, u8"Чтобы пользоваться ботом, необходимо пройти регистрацию (Введите /start)");
                }
                db.exec("COMMIT");
            }
            catch (std::exception& e) {
                std::cerr << "Exception occurred: " << e.what() << std::endl;
                db.exec("ROLLBACK");
            }
            });
        bot.getEvents().onCommand("mytime", [&bot, &db](TgBot::Message::Ptr message) {
            try {
                db.exec("BEGIN TRANSACTION");

                if (registration(db, message) == true) {
                    SQLite::Statement query(db, "SELECT COUNT(*) FROM users WHERE id = ? and available = 0");
                    query.bind(1, message->chat->id);
                    if (query.executeStep()) {
                        int count = query.getColumn(0).getInt();
                        if (count > 0) {
                            SQLite::Statement dayntime(db, "SELECT day_and_time, type FROM users WHERE id = ?");
                            dayntime.bind(1, message->chat->id);

                            if (dayntime.executeStep()) {
                                std::string day_and_time = dayntime.getColumn(0).getText();
                                day_and_time += ", ";
                                day_and_time += dayntime.getColumn(1).getText();
                                bot.getApi().sendMessage(message->chat->id, u8"Ваши записи:\n" + day_and_time);
                            }
                            else {
                                bot.getApi().sendMessage(message->chat->id, u8"Произошла ошибка при получении данных.");
                            }
                        }
                        else {
                            bot.getApi().sendMessage(message->chat->id, u8"У вас нет ни одной записи. Чтобы записаться используйте команду /time");
                        }
                    }
                }
                else {
                    bot.getApi().sendMessage(message->chat->id, u8"Чтобы пользоваться ботом, необходимо пройти регистрацию (Введите /start)");
                }
                db.exec("COMMIT");
            }
            catch (std::exception& e) {
                std::cerr << "Exception occurred: " << e.what() << std::endl;
                db.exec("ROLLBACK");
            }
            });

        bot.getEvents().onAnyMessage([&bot, &db](TgBot::Message::Ptr message) {

            long chatId = message->chat->id;
            if (sessions.find(chatId) == sessions.end()) {
                // Если сессии нет, создаем новую
                sessions[chatId] = UserData();
            }
            UserData& userData = sessions[chatId];

            if ((StringTools::startsWith(message->text, "/start")) || (StringTools::startsWith(message->text, "/time")) || (StringTools::startsWith(message->text, "/cancel")) || (StringTools::startsWith(message->text, "/mytime"))) {
                return;
            }
            else if (userData.registration) {
                try {

                    // Проверяем, заполнен ли номер телефона, если нет - заполняем
                    if (userData.phoneNumber.empty()) {
                        userData.phoneNumber = message->text;
                        if (!isValidPhoneNumber(userData.phoneNumber)) {
                            bot.getApi().sendMessage(chatId, u8"Неккоректный номер, введите еще раз.");
                            userData.phoneNumber.clear();
                        }
                        else {
                            bot.getApi().sendMessage(chatId, u8"Спасибо! Теперь введите ваше имя:");
                        }
                    }
                    else {
                        userData.name = message->text;
                        db.exec("BEGIN TRANSACTION");
                        SQLite::Statement users(db, "INSERT INTO users (id, name, phone, available) VALUES (?, ?, ?, 1)");
                        users.bind(1, message->chat->id);
                        users.bind(2, userData.name);
                        users.bind(3, userData.phoneNumber);
                        users.exec();

                        // При заполнении всех данных можно отправить сообщение о завершении регистрации
                        bot.getApi().sendMessage(chatId, u8"Регистрация завершена. Добро пожаловать, " + userData.name + "!");
                        userData.registration = false;
                        db.exec("COMMIT");
                    }
                }
                catch (std::exception& e) {
                    std::cerr << "Exception occurred: " << e.what() << std::endl;
                    db.exec("ROLLBACK");
                }
            }
            });

        try {
            printf("Bot username: %s\n", bot.getApi().getMe()->username.c_str());
            TgBot::TgLongPoll longPoll(bot);
            while (true) {
                printf("Long poll started\n");
                longPoll.start();
            }
        }
        catch (TgBot::TgException& e) {
            printf("error: %s\n", e.what());
        }
    }
    return 0;
}