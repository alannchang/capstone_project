from transformers import pipeline

summarizer = pipeline("summarization", model="Falconsai/text_summarization")

while True:
    article = input("What do you need summarized?" + '\n')
    print(summarizer(article, max_length=1000, min_length=100, do_sample=False))
