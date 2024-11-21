const toggleBtn = document.querySelector('#dark_mode_toggle_button')
const btnIcon = document.querySelector('#button_icon')
const htmlElement = document.querySelector('html')

// Initialize the theme based on the saved value in localStorage
let currentTheme = localStorage.getItem('theme') ?? 'light'
if (currentTheme === 'light') {
    btnIcon.setAttribute('class', 'bx bxs-moon')
} else {
    htmlElement.classList.add('dark')
    btnIcon.setAttribute('class', 'bx bxs-sun')
}

// Toggle theme on button click
toggleBtn.addEventListener('click', () => {
    currentTheme = localStorage.getItem('theme') ?? 'light'
    if (currentTheme === 'light') {
        htmlElement.classList.add('dark')
        btnIcon.setAttribute('class', 'bx bxs-sun')
        localStorage.setItem('theme', 'dark')
        console.log('Switched to dark mode')
    } else {
        htmlElement.classList.remove('dark')
        btnIcon.setAttribute('class', 'bx bxs-moon')
        localStorage.setItem('theme', 'light')
        console.log('Switched to light mode')
    }
})
