import os
import base64
import time
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError
from email.mime.text import MIMEText

class GmailManager:
    def __init__(self, credentials_path='resources/credentials.json', token_path='resources/token.json'):
        """
        Initialize Gmail API service with OAuth 2.0 authentication
        
        :param credentials_path: Path to OAuth 2.0 credentials file
        :param token_path: Path to store access token
        """
        self.SCOPES = [
            'https://www.googleapis.com/auth/gmail.readonly',  # Read emails
            'https://www.googleapis.com/auth/gmail.send',      # Send emails
            'https://www.googleapis.com/auth/gmail.modify',    # Modify emails
            'https://www.googleapis.com/auth/gmail.labels'     # Access to labels
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
    
    """ USER PROFILE """
    
    def get_profile(self):
        """
        Get the user's Gmail profile
        
        :return: Gmail profile information
        """
        try:
            profile = self.service.users().getProfile(userId='me').execute()
            return profile
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'error': str(e)}

    """ DRAFTS """

    """ HISTORY """

    def get_history(self, start_history_id=None, max_results=100):
        """
        Get the history of changes to the user's mailbox.
        
        :param start_history_id: Starting point for fetching history (optional)
        :param max_results: Maximum number of history records to return
        :return: List of history records
        """
        try:
            # If no start_history_id is provided, get the current one
            if not start_history_id:
                profile = self.get_profile()
                start_history_id = profile.get('historyId', 1)
                # Wait a moment to ensure there might be some history to fetch
                time.sleep(1)
            
            params = {
                'userId': 'me',
                'startHistoryId': start_history_id,
                'maxResults': max_results,
                'historyTypes': ['messageAdded', 'messageDeleted', 'labelAdded', 'labelRemoved']
            }
            
            history = self.service.users().history().list(**params).execute()
            
            return {
                'history_id': start_history_id,
                'history_records': history.get('history', []),
                'next_page_token': history.get('nextPageToken', None)
            }
            
        except HttpError as e:
            print(f"An error occurred: {e}")
            if e.resp.status == 404:
                print("History ID not found. The ID might be too old.")
            return {'error': str(e)}

    """ LABELS """

    def list_labels(self):
        """
        List all labels in the user's mailbox
        
        :return: List of labels
        """
        try:
            results = self.service.users().labels().list(userId='me').execute()
            labels = results.get('labels', [])
            return labels
        except HttpError as e:
            print(f"An error occurred: {e}")
            return []

    def get_label(self, label_id):
        """
        Get details for a specific label
        
        :param label_id: ID of the label to retrieve
        :return: Label details
        """
        try:
            label = self.service.users().labels().get(userId='me', id=label_id).execute()
            return label
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'error': str(e)}

    def create_label(self, name, label_list_visibility='labelShow', message_list_visibility='show'):
        """
        Create a new label
        
        :param name: Name of the label
        :param label_list_visibility: Visibility in the label list (labelShow/labelHide/labelShowIfUnread)
        :param message_list_visibility: Visibility in the message list (show/hide)
        :return: Created label details
        """
        try:
            label_object = {
                'name': name,
                'labelListVisibility': label_list_visibility,
                'messageListVisibility': message_list_visibility
            }
            
            created_label = self.service.users().labels().create(
                userId='me', 
                body=label_object
            ).execute()
            
            return created_label
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'error': str(e)}

    def update_label(self, label_id, name=None, label_list_visibility=None, message_list_visibility=None):
        """
        Update an existing label
        
        :param label_id: ID of the label to update
        :param name: New name for the label (optional)
        :param label_list_visibility: New visibility in label list (optional)
        :param message_list_visibility: New visibility in message list (optional)
        :return: Updated label details
        """
        try:
            # Get the current label first
            current_label = self.get_label(label_id)
            if 'error' in current_label:
                return current_label
            
            # Update only the provided fields
            if name:
                current_label['name'] = name
            if label_list_visibility:
                current_label['labelListVisibility'] = label_list_visibility
            if message_list_visibility:
                current_label['messageListVisibility'] = message_list_visibility
            
            updated_label = self.service.users().labels().update(
                userId='me', 
                id=label_id, 
                body=current_label
            ).execute()
            
            return updated_label
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'error': str(e)}

    def delete_label(self, label_id):
        """
        Delete a label
        
        :param label_id: ID of the label to delete
        :return: Success status
        """
        try:
            self.service.users().labels().delete(userId='me', id=label_id).execute()
            return {'success': True, 'message': f'Label {label_id} deleted successfully'}
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'success': False, 'error': str(e)}

    """ MESSAGES """

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
        
        except HttpError as e:
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
        
        except HttpError as e:
            print(f"An error occurred: {e}")
            return None

def main():
    # Initialize Gmail Manager
    gmail_manager = GmailManager()
    
    # Example: Get user profile
    print("\nUser Profile:")
    profile = gmail_manager.get_profile()
    print(f"Email: {profile.get('emailAddress')}")
    print(f"Messages Total: {profile.get('messagesTotal')}")
    print(f"Threads Total: {profile.get('threadsTotal')}")
    print(f"History ID: {profile.get('historyId')}")
    
    # Example: List all labels
    print("\nAll Labels:")
    labels = gmail_manager.list_labels()
    for label in labels:
        print(f"Label: {label['name']} (ID: {label['id']})")
    
    # Example: Get history
    print("\nHistory:")
    history_data = gmail_manager.get_history()
    print(f"Starting from History ID: {history_data['history_id']}")
    print(f"Number of history records: {len(history_data.get('history_records', []))}")
    if history_data.get('history_records'):
        for record in history_data['history_records'][:3]:  # Show first 3 records
            print(f"Record ID: {record.get('id')}")
            
    # Example: List recent emails
    print("\nRecent Emails:")
    recent_emails = gmail_manager.list_messages(max_results=5)
    for email in recent_emails:
        print(f"From: {email['sender']}")
        print(f"Subject: {email['subject']}")
        print(f"Snippet: {email['snippet']}\n")
    
    # Uncomment to test additional features
    # Example: Create a new label
    # new_label = gmail_manager.create_label("Test Label")
    # print(f"Created label: {new_label.get('name')} (ID: {new_label.get('id')})")
    
    # Example: Send a test email
    # gmail_manager.send_message(
    #     to='recipient@example.com', 
    #     subject='Test Email', 
    #     body='This is a test email sent via Gmail API'
    # )

if __name__ == '__main__':
    main()
