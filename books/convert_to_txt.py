import html
import os
import re
import xml.etree.ElementTree as ET
import zipfile
from pathlib import PurePosixPath

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BOOKS_FOLDER = os.path.join(SCRIPT_DIR, "books")
BOOKS_TXT_FOLDER = os.path.join(SCRIPT_DIR, "books_txt")
NEW_PAGE_MARKER = "<New_Page>"


def read_epub_file(epub_file_path: str) -> list[list[str]]:
    # Same parsing as run_ui.py (copied, because importing run_ui would start the GUI).
    # Returns the book as a list of pages (one per chapter of the EPUB spine), each page a list of words
    with zipfile.ZipFile(epub_file_path) as epub_zip:
        container_xml = ET.fromstring(epub_zip.read("META-INF/container.xml"))
        rootfile_path = container_xml.find(
            ".//{urn:oasis:names:tc:opendocument:xmlns:container}rootfile"
        )

        if rootfile_path is None:
            raise ValueError("EPUB package file not found")

        package_path = rootfile_path.attrib.get("full-path", "")
        package_dir = PurePosixPath(package_path).parent
        package_xml = ET.fromstring(epub_zip.read(package_path))

        namespace = {"opf": "http://www.idpf.org/2007/opf"}
        manifest_by_id = {
            item.attrib["id"]: item.attrib["href"]
            for item in package_xml.findall(".//opf:manifest/opf:item", namespace)
            if item.attrib.get("id") and item.attrib.get("href")
        }

        pages: list[list[str]] = []
        for item_ref in package_xml.findall(".//opf:spine/opf:itemref", namespace):
            item_id = item_ref.attrib.get("idref")
            if not item_id or item_id not in manifest_by_id:
                continue

            chapter_path = package_dir / manifest_by_id[item_id]
            chapter_text = epub_zip.read(str(chapter_path)).decode("utf-8", errors="ignore")
            chapter_text = re.sub(r"<[^>]+>", " ", chapter_text)
            chapter_text = html.unescape(chapter_text)
            words = re.findall(r"\S+", chapter_text)

            pages.append(words)

        return pages


def convert_book(epub_path: str, txt_path: str) -> int:
    pages = read_epub_file(epub_path)
    with open(txt_path, "w", encoding="utf-8") as txt_file:
        for page_index, words in enumerate(pages):
            if page_index > 0:
                txt_file.write("\n" + NEW_PAGE_MARKER + "\n")
            txt_file.write(" ".join(words))
        txt_file.write("\n")
    return len(pages)


def main():
    os.makedirs(BOOKS_TXT_FOLDER, exist_ok=True)
    books = sorted(f for f in os.listdir(BOOKS_FOLDER) if f.lower().endswith(".epub"))

    for book in books:
        epub_path = os.path.join(BOOKS_FOLDER, book)
        txt_path = os.path.join(BOOKS_TXT_FOLDER, os.path.splitext(book)[0] + ".txt")
        try:
            page_count = convert_book(epub_path, txt_path)
            print(f"OK    {book} -> {page_count} pages")
        except Exception as error:
            print(f"ERROR {book}: {error}")

    print(f"Done: {len(books)} book(s) processed, output in {BOOKS_TXT_FOLDER}")


if __name__ == "__main__":
    main()
