import os
import base64
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build
from email.mime.text import MIMEText

class GmailManager:
    def __init__(self, credentials_path='credentials.json', token_path='token.json'):
        """
        Initialize Gmail API service with OAuth 2.0 authentication
        
        :param credentials_path: Path to OAuth 2.0 credentials file
        :param token_path: Path to store access token
        """
        self.SCOPES = [
            'https://www.googleapis.com/auth/gmail.readonly',  # Read emails
            'https://www.googleapis.com/auth/gmail.send',      # Send emails
            'https://www.googleapis.com/auth/gmail.modify'     # Modify emails
        ]
        
        self.credentials_path = credentials_path
        self.token_path = token_path
        self.service = self._get_gmail_service()

    def _get_gmail_service(self):
        """
        Authenticate and create Gmail API service
        
        :return: Gmail API service object
        """
        creds = None
        
        # Check if token exists
        if os.path.exists(self.token_path):
            creds = Credentials.from_authorized_user_file(self.token_path, self.SCOPES)
        
        # If no valid credentials, initiate OAuth flow
        if not creds or not creds.valid:
            flow = InstalledAppFlow.from_client_secrets_file(
                self.credentials_path, self.SCOPES)
            creds = flow.run_local_server(port=0)
            
            # Save credentials for next run
            with open(self.token_path, 'w') as token:
                token.write(creds.to_json())
        
        return build('gmail', 'v1', credentials=creds)

    def list_messages(self, query='', max_results=10):
        """
        List messages from Gmail inbox
        
        :param query: Optional search query to filter messages
        :param max_results: Maximum number of messages to retrieve
        :return: List of message details
        """
        try:
            results = self.service.users().messages().list(
                userId='me', 
                q=query, 
                maxResults=max_results
            ).execute()
            
            messages = results.get('messages', [])
            
            detailed_messages = []
            for msg in messages:
                txt = self.service.users().messages().get(
                    userId='me', 
                    id=msg['id']
                ).execute()
                
                try:
                    payload = txt['payload']
                    headers = payload['headers']
                    
                    # Extract subject and sender
                    subject = next((h['value'] for h in headers if h['name'] == 'Subject'), 'No Subject')
                    sender = next((h['value'] for h in headers if h['name'] == 'From'), 'Unknown Sender')
                    
                    # Decode message body
                    if 'parts' in payload:
                        body = payload['parts'][0]['body'].get('data', '')
                    else:
                        body = payload.get('body', {}).get('data', '')
                    
                    body = base64.urlsafe_b64decode(body + '===').decode('utf-8') if body else 'No body'
                    
                    detailed_messages.append({
                        'id': msg['id'],
                        'subject': subject,
                        'sender': sender,
                        'snippet': body[:100] + '...' if len(body) > 100 else body
                    })
                except Exception as detail_error:
                    print(f"Error processing message details: {detail_error}")
            
            return detailed_messages
        
        except Exception as e:
            print(f"An error occurred: {e}")
            return []

    def send_message(self, to, subject, body):
        """
        Send an email using Gmail API
        
        :param to: Recipient email address
        :param subject: Email subject
        :param body: Email body text
        :return: Sent message ID or None
        """
        try:
            message = MIMEText(body)
            message['to'] = to
            message['subject'] = subject
            raw_message = base64.urlsafe_b64encode(message.as_bytes()).decode('utf-8')
            
            send_message = self.service.users().messages().send(
                userId='me', 
                body={'raw': raw_message}
            ).execute()
            
            print(f"Message sent. Message ID: {send_message['id']}")
            return send_message['id']
        
        except Exception as e:
            print(f"An error occurred: {e}")
            return None

def main():
    # Initialize Gmail Manager
    gmail_manager = GmailManager()
    
    # Example: List recent emails
    print("Recent Emails:")
    recent_emails = gmail_manager.list_messages(max_results=5)
    for email in recent_emails:
        print(f"From: {email['sender']}")
        print(f"Subject: {email['subject']}")
        print(f"Snippet: {email['snippet']}\n")
    
    # Example: Send a test email
    # Uncomment and replace with actual email details
    # gmail_manager.send_message(
    #     to='recipient@example.com', 
    #     subject='Test Email', 
    #     body='This is a test email sent via Gmail API'
    # )

if __name__ == '__main__':
    main()
