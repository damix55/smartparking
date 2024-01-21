import logging
import yaml
import os
import requests
import json
from telegram.ext import Updater, CommandHandler, MessageHandler, Filters, CallbackContext, CallbackQueryHandler
from telegram import KeyboardButton, InlineKeyboardButton, InlineKeyboardMarkup, ReplyKeyboardMarkup, Update


class ParkingBot:

    parking_lots = {}

    def __init__(self):
        dir_path = os.path.dirname(os.path.realpath(__file__))
        with open(f"{dir_path}/config.yml", 'r') as stream:
            try:
               self.config = yaml.safe_load(stream)
            except yaml.YAMLError as exc:
                print(exc)

        # Enable logging
        logging.basicConfig(format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                            level=logging.INFO)

        self.logger = logging.getLogger(__name__)

        self.commands = [
            {
                'func': self.book,
                'cmd': 'book',
                'txt': '🚗 Prenota un posto',
                'desc': 'per prenotare un posto'
            },
            {
                'func': self.bookings,
                'cmd': 'bookings',
                'txt': '☰ Prenotazioni effettuate',
                'desc': 'per visualizzare le tue prenotazioni effettuate'
            },
            {
                'func': self.delete,
                'cmd': 'delete',
                'txt': '❌ Cancella prenotazione',
                'desc': 'per cancellare una prenotazione effettuata in precedenza'
            },
            {
                'func': self.help,
                'cmd': 'help',
                'txt': '❓ Aiuto',
                'desc': 'per ottenere informazioni sui comandi utilizzabili'
            }
        ]


    def update_parking_lots(self):
        self.parking_lots = requests.get(self.config['flask']).json()


    def start(self, update, context):
        kb = [[KeyboardButton(x['txt'])] for x in self.commands]
        kb_markup = ReplyKeyboardMarkup(kb)
        context.bot.send_message(chat_id=update.message.chat_id,
                        text=self.help_text(),
                        reply_markup=kb_markup)


    def book(self, update, context):

        self.update_parking_lots()

        keyboard = []

        user = update.message.chat.username

        for c in self.parking_lots.values():
            free = c['free_parkings']
            circle = '🟢'
            if free == 0:
                circle = '🔴'

            keyboard.append([InlineKeyboardButton(
                f'{circle} {c["name"]} ({free})',
                callback_data=json.dumps({
                    'a': 'book',
                    'p': c['id'],
                    'u': user
                })
            )])

        kb_markup = InlineKeyboardMarkup(keyboard)
        context.bot.send_message(chat_id=update.message.chat_id,
                        text="Seleziona un parcheggio:",
                        reply_markup=kb_markup)

    
    def bookings(self, update, context):
        user = update.message.chat.username
        booked = requests.get(f"{self.config['flask']}/bookings/{user}").json()['bookings']
        self.update_parking_lots()

        if len(booked) == 0:
            text = 'Nessuna prenotazione effettuata.'

        else:
            text = 'Prenotazioni effettuate:'
            for x in booked:
                text += f"\n • {self.parking_lots[x]['name']}"

        context.bot.send_message(chat_id=update.message.chat_id, text=text)


    def delete(self, update, context):
        user = update.message.chat.username
        booked = requests.get(f"{self.config['flask']}/bookings/{user}").json()['bookings']
        self.update_parking_lots()

        

        if len(booked) == 0:
            context.bot.send_message(chat_id=update.message.chat_id,
                            text='Nessuna prenotazione effettuata.')

        else:
            keyboard = []
            for b in booked:
                keyboard.append([InlineKeyboardButton(
                    self.parking_lots[b]["name"],
                    callback_data=json.dumps({
                        'a': 'delete',
                        'p': b,
                        'u': user
                    })
                )])

            kb_markup = InlineKeyboardMarkup(keyboard)
            
            context.bot.send_message(chat_id=update.message.chat_id,
                            text="Seleziona una prenotazione da cancellare:",
                            reply_markup=kb_markup)
                        

    def help_text(self):
        text = ''
        for c in self.commands:
            text += f"/{c['cmd']} {c['desc']}\n"
        return text


    def help(self, update, context):
        context.bot.send_message(chat_id=update.message.chat_id, text=self.help_text())


    def button(self, update: Update, context: CallbackContext) -> None:
        """Parses the CallbackQuery and updates the message text."""
        query = update.callback_query

        # CallbackQueries need to be answered, even if no notification to the user is needed
        # Some clients may have trouble otherwise. See https://core.telegram.org/bots/api#callbackquery
        query.answer()

        data = json.loads(query.data)

        parking_id = data['p']
        user = data['u']
        parking = self.parking_lots[parking_id]

        if data['a'] == 'book':
            if parking['free_parkings'] != 0:
                post_data = {'book': user}
                req = requests.post(f"{self.config['flask']}/parking/{parking_id}/book", json=post_data)
                if req.status_code==200:
                    query.edit_message_text(text=f'Prenotazione per {parking["name"]} effettuata con successo!')
                else:
                    query.edit_message_text(text=f'C\'è stato un problema nella prenotazione, si prega di riprovare più tardi.')
            
            else:
                query.edit_message_text(text=f'Impossibile prenotare poichè il parcheggio è pieno.')
        
        elif data['a'] == 'delete':
            post_data = {'book': user}
            req = requests.post(f"{self.config['flask']}/parking/{parking_id}/unbook", json=post_data)
            if req.status_code==200:
                query.edit_message_text(text=f'Prenotazione per {parking["name"]} cancellata con successo!')
            else:
                query.edit_message_text(text=f'C\'è stato un problema nella cancellazione della prenotazione, si prega di riprovare più tardi.')


    def text(self, update, context):
        for c in self.commands:
            if c['txt'] == update.message.text:
                c['func'](update, context)


    def error(self, update, context):
        """Log Errors caused by Updates."""
        self.logger.warning('Update "%s" caused error "%s"', update, context.error)


    def main(self):

        updater = Updater(self.config['token'], use_context=True)

        # Get the dispatcher to register handlers
        dp = updater.dispatcher

        dp.add_handler(CommandHandler('start', self.start))

        for c in self.commands:
            dp.add_handler(CommandHandler(c['cmd'], c['func']))


        dp.add_handler(CallbackQueryHandler(self.button))

        # on noncommand i.e message - echo the message on Telegram
        dp.add_handler(MessageHandler(Filters.text, self.text))

        # log all errors
        dp.add_error_handler(self.error)

        # Start the Bot
        updater.start_polling()

        # Run the bot until you press Ctrl-C or the process receives SIGINT,
        # SIGTERM or SIGABRT. This should be used most of the time, since
        # start_polling() is non-blocking and will stop the bot gracefully.
        updater.idle()


if __name__ == '__main__':
    ParkingBot().main()